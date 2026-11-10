#include "PlaybookEngine.h"
#include <cstring>
#include <cmath>

namespace hammer
{
void PlaybookEngine::reset()
{
    grooveIdx = -1;
    styleOfGroove = -1;
    grooveName = "";
}

void PlaybookEngine::renderGroove (const GrooveDef& g, int tpb, int bpb, bool halfTime, bool doubleTime,
                                   int64_t barIndex, LaneGrid& out)
{
    out.clear (tpb);
    const int spb = std::max (1, g.steps / 4);          // steps per beat in the source string
    const int tickPerStep = kTicksPerBeat / spb;         // 3 (16ths), 4 (triplets), 2 (16th trips)
    const int srcTicks = 4 * kTicksPerBeat;              // grooves are written as 4/4

    for (const auto& lp : g.lanes)
    {
        if (lp.lane < 0 || lp.steps == nullptr) continue;
        const int len = (int) std::strlen (lp.steps);
        for (int i = 0; i < len && i < g.steps; ++i)
        {
            float p, v;
            if (! StyleLibrary::decodeStep (lp.steps[i], p, v)) continue;
            int srcTick = i * tickPerStep;

            if (halfTime)
            {
                // A 4/4 groove spans two bars: even bars play its first half, odd bars the second.
                const int half = srcTicks / 2;
                const bool second = (barIndex & 1) != 0;
                if (second != (srcTick >= half)) continue;
                srcTick = (srcTick - (second ? half : 0)) * 2;
            }
            else if (doubleTime)
            {
                // Only 8th-level events survive, compressed to 16ths, played twice.
                if (srcTick % 6 != 0) continue;
                srcTick /= 2;
                for (int rep = 0; rep < 2; ++rep)
                {
                    const int t0 = srcTick + rep * (srcTicks / 2);
                    for (int b = 0; b * 4 * kTicksPerBeat < tpb + 4 * kTicksPerBeat; ++b)
                    {
                        const int t = t0 + b * srcTicks;
                        if (t < tpb) out.set (lp.lane, t, p, v);
                    }
                }
                continue;
            }
            // Tile the 4/4 source across the bar's beats (3/4 uses beats 1-3, 6/4 repeats ...)
            for (int t = srcTick; t < tpb; t += srcTicks)
                out.set (lp.lane, t, p, v);
            (void) bpb;
        }
    }
}

void PlaybookEngine::generate (const BrainContext& ctx, LaneGrid& g, Rng& rng)
{
    const StyleDef& s = *ctx.style;

    // ---- groove choice with hysteresis (drummers don't switch beats every bar)
    const int want = StyleLibrary::grooveForIntensity (s, ctx.intensity);
    if (grooveIdx < 0 || styleOfGroove != ctx.styleId)
    {
        grooveIdx = want;
        styleOfGroove = ctx.styleId;
    }
    else if (ctx.phraseStart || ctx.sectionChange)
    {
        const float d = std::abs (s.grooves[grooveIdx].intensity - ctx.intensity);
        if (d > 0.11f || ctx.sectionChange) grooveIdx = want;
        // an improvising drummer sometimes moves to a neighbouring groove
        else if (rng.chance (ctx.improvise * 0.25f))
        {
            const int n = grooveIdx + (rng.chance (0.5f) ? 1 : -1);
            if (n >= 0 && n < s.numGrooves && std::abs (s.grooves[n].intensity - ctx.intensity) < 0.25f) grooveIdx = n;
        }
    }
    if (ctx.forceHalfTimeGroove)
    {
        for (int i = 0; i < s.numGrooves; ++i)
            if (s.grooves[i].halfTime) { grooveIdx = i; break; }
    }
    grooveIdx = std::max (0, std::min (s.numGrooves - 1, grooveIdx));
    const GrooveDef& gr = s.grooves[grooveIdx];
    grooveName = gr.name;

    const bool half = ctx.halfTime || (ctx.forceHalfTimeGroove && ! gr.halfTime);
    renderGroove (gr, ctx.ticksPerBar, ctx.beatsPerBar, half, ctx.doubleTime && ! half, ctx.barIndex, g);

    applyListening (ctx, g);
    applyOrnaments (ctx, g, rng);
    applyAggression (ctx, g, rng);
}

// ----------------------------------------------------------------------------
void PlaybookEngine::applyListening (const BrainContext& ctx, LaneGrid& g)
{
    if (ctx.accents == nullptr || ! ctx.lf.hasData) return;
    const float f = ctx.follow;
    if (f <= 0.001f) return;
    const int tpb = ctx.ticksPerBar;
    const int grid = ctx.style->tripletGrid ? 4 : 3;
    const float (*a)[kMaxTicks] = ctx.accents;

    // How "pulse-like" is the groove's kick? Continuous double-kick / four-on-the-floor
    // patterns are style features and must not be thinned out by listening.
    int kicks = 0, onBeatKicks = 0;
    for (int t = 0; t < tpb; t += 3) if (g.prob[Kick][t] > 0.5f) { ++kicks; if (t % kTicksPerBeat == 0) ++onBeatKicks; }
    const float pulse = clamp01 ((float) kicks / (float) std::max (1, tpb / 3) * 2.f);
    const bool fourFloor = onBeatKicks >= ctx.beatsPerBar;
    // jazz/blues: the bass drum feathers and drops "bombs", it doesn't double the bass line
    const float kickFollow = ctx.style->tripletGrid ? 0.45f : 1.f;
    float stabFactor = 1.f;
    switch (ctx.styleId)
    {
        case Orchestral: case Minimal: stabFactor = 0.35f; break;
        case Jazz: case Blues: case FusionJazz: stabFactor = 0.5f; break;
        default: break;
    }
    int stabCrashes = 0;

    for (int t = 0; t < tpb; ++t)
    {
        const float lo = a[0][t], mi = a[1][t], hi = a[2][t];
        const bool onGrid = (t % grid) == 0;
        const bool onBeat = (t % kTicksPerBeat) == 0;

        // ---- KICK locks to the low end (bass + kick of the track)
        if (onGrid)
        {
            float& p = g.prob[Kick][t];
            float& v = g.vel[Kick][t];
            const float grooveP = p;
            const float kf = f * kickFollow;
            float red = 0.6f * kf * (1.f - pulse);
            if (onBeat && (fourFloor || t == 0)) red *= 0.2f; // the one (and a floor pulse) stays
            const float np = grooveP * (1.f - red) + kf * lo * 1.05f;
            p = clamp01 (std::max (np, grooveP * (1.f - red)));
            if (lo > 0.3f && kf > 0.f) v = std::max (v, lerp (v, 0.55f + 0.45f * lo, kf));
        }
        // ---- SNARE catches strong mid stabs off the backbeat
        if (onGrid && mi > 0.45f)
        {
            const float extra = f * (mi - 0.3f) * 1.1f * (lo > 0.4f ? 1.f : 0.6f);
            if (extra > g.prob[Snare][t])
            {
                g.prob[Snare][t] = clamp01 (extra);
                g.vel[Snare][t] = std::max (g.vel[Snare][t], 0.55f + 0.4f * mi);
            }
        }
        // ---- CRASH on unison band hits (stabs / hits with the band) - sparingly
        const float uni = std::min (lo, std::min (mi, hi));
        const bool strongPos = (t % (kTicksPerBeat / 2)) == 0; // beats and 8ths
        if (onGrid && strongPos && uni > 0.55f && ctx.intensity > 0.4f && stabCrashes < 1)
        {
            const float pc = f * stabFactor * (uni - 0.35f) * 1.6f;
            if (pc > 0.5f && pc > g.prob[Crash][t])
            {
                g.prob[Crash][t] = clamp01 (pc);
                g.vel[Crash][t] = 0.7f + 0.3f * uni;
                g.prob[Kick][t] = std::max (g.prob[Kick][t], clamp01 (pc + 0.2f));
                g.vel[Kick][t] = std::max (g.vel[Kick][t], 0.85f);
                ++stabCrashes;
            }
        }
        // ---- HATS follow high-band subdivision (16ths when the track is busy up top)
        const bool sixteenthPos = ! ctx.style->tripletGrid && (t % 3 == 0) && (t % 6 != 0);
        if (sixteenthPos && ctx.lf.sixteenthFeel > 0.45f && hi > 0.2f && f > 0.25f)
        {
            int hatLane = -1;
            if (g.prob[HatClosed][t - 3] > 0.5f || (t + 3 < tpb && g.prob[HatClosed][t + 3] > 0.5f)) hatLane = HatClosed;
            else if (g.prob[Ride][t - 3] > 0.5f) hatLane = Ride;
            if (hatLane >= 0)
                g.set (hatLane, t, f * 0.8f * ctx.lf.sixteenthFeel, 0.35f + 0.3f * hi);
        }
        // ---- velocity shaping: accents of the track make our hits push harder
        const float acc = std::max (lo, std::max (mi * 0.9f, hi * 0.6f));
        for (int l = 0; l < NumLanes; ++l)
            if (g.prob[l][t] > 0.f)
                g.vel[l][t] = clamp01 (g.vel[l][t] * (0.88f + 0.28f * f * acc + (onBeat ? 0.02f : 0.f)));
    }
}

// ----------------------------------------------------------------------------
void PlaybookEngine::applyOrnaments (const BrainContext& ctx, LaneGrid& g, Rng& rng)
{
    const auto& o = ctx.style->orn;
    const float im = ctx.improvise, in = ctx.intensity;
    const int tpb = ctx.ticksPerBar;
    const bool lastBarOfPhrase = ctx.barInPhrase == ctx.phraseBars - 1;

    // ghost snares around backbeats
    if (o.ghostSnare > 0.f && in > 0.15f)
    {
        const float pg = o.ghostSnare * (0.25f + 0.75f * im) * (0.5f + 0.5f * in);
        for (int t = 3; t < tpb; t += 3)
        {
            if (t % 6 == 0 && (t % kTicksPerBeat) == 0) continue;
            if (g.prob[Snare][t] > 0.f || g.prob[Kick][t] > 0.7f) continue;
            const bool nearBack = (t % (2 * kTicksPerBeat)) >= 9 || (t % (2 * kTicksPerBeat)) <= 3 || (t % kTicksPerBeat) == 9;
            g.set (Snare, t, pg * (nearBack ? 0.55f : 0.25f), 0.2f + 0.1f * rng.uni());
        }
    }
    // kick pickups into beat 1 and 3
    if (o.kickPickup > 0.f)
    {
        const int grid = ctx.style->tripletGrid ? 4 : 3;
        const int targets[2] = { tpb - grid, std::min (tpb - 1, 2 * kTicksPerBeat - grid) };
        for (int t : targets)
            if (t > 0 && g.prob[Kick][t] < 0.5f)
                g.set (Kick, t, o.kickPickup * im * 0.6f, 0.6f);
    }
    // John Bonham: single-foot 16th-triplet kick doubles leading into a beat ("ba-da-BOOM").
    // The second note is the stronger one, pulling into the next beat.
    if (o.kickTriplets > 0.f && ! ctx.style->tripletGrid && in > 0.3f)
    {
        const float pk = o.kickTriplets * (0.3f + 0.7f * im) * (0.4f + 0.6f * in);
        for (int b = 1; b <= ctx.beatsPerBar; ++b)
        {
            const int beat = b * kTicksPerBeat;           // the beat being pulled into
            const int t1 = beat - 4, t2 = beat - 2;       // 16th-triplet partials
            if (t1 < 0 || g.prob[Snare][t1] > 0.3f || g.prob[Snare][t2] > 0.3f) continue;
            if (! rng.chance (pk * 0.4f)) continue;
            g.set (Kick, t1, 1.f, 0.66f);
            g.set (Kick, t2, 1.f, 0.82f);
        }
    }

    // open hat at the end of a phrase
    if (o.openHatEnd > 0.f && lastBarOfPhrase)
    {
        const int t = tpb - 6;
        if (g.prob[HatClosed][t] > 0.5f && rng.chance (o.openHatEnd * (0.5f + 0.5f * im)))
        {
            g.prob[HatOpen][t] = 1.f; g.vel[HatOpen][t] = g.vel[HatClosed][t];
            g.prob[HatClosed][t] = 0.f;
        }
    }
    // 16th hat subdivision at higher intensity
    if (o.hatSubdiv > 0.f && in > 0.55f && ! ctx.style->tripletGrid)
    {
        const float ps = o.hatSubdiv * (in - 0.5f) * 2.f * (0.4f + 0.6f * im);
        for (int t = 3; t < tpb; t += 6)
            if (g.prob[HatClosed][t - 3] > 0.5f && g.prob[HatClosed][t] < 0.5f)
                g.set (HatClosed, t, ps, 0.4f);
    }
    // jazz comping: conversational snare & kick on triplet partials
    if (o.comping > 0.f)
    {
        for (int t = 0; t < tpb; t += 4)
        {
            if (t % kTicksPerBeat == 0 && rng.chance (0.6f)) continue; // comping lives between beats mostly
            const float pc = o.comping * (0.06f + 0.22f * in) * (0.35f + im);
            g.set (Snare, t, pc, 0.25f + 0.3f * rng.uni() + 0.2f * in);
            if (o.bombs > 0.f)
                g.set (Kick, t, o.bombs * 0.07f * (0.3f + in) * (0.3f + im), 0.55f + 0.3f * in);
        }
    }
    // ride bell accents
    if (o.rideBell > 0.f)
        for (int t = 0; t < tpb; t += kTicksPerBeat)
            if (g.prob[Ride][t] > 0.5f && rng.chance (o.rideBell * im * 0.35f))
            { g.prob[RideBell][t] = 1.f; g.vel[RideBell][t] = 0.8f; g.prob[Ride][t] = 0.f; }
    // splash
    if (o.splash > 0.f && rng.chance (o.splash * im * 0.15f))
    {
        const int t = (1 + rng.below (std::max (1, tpb / 6 - 1))) * 6;
        if (t < tpb) g.set (Splash, t, 1.f, 0.7f);
    }
}

// ----------------------------------------------------------------------------
void PlaybookEngine::applyAggression (const BrainContext& ctx, LaneGrid& g, Rng& rng)
{
    const auto& o = ctx.style->orn;
    const float ag = ctx.aggression;
    const int tpb = ctx.ticksPerBar;

    // double-kick runs
    if (o.doubleKick > 0.f && ag > 0.55f && ! ctx.style->tripletGrid)
    {
        const float pd = o.doubleKick * (ag - 0.55f) * 2.2f;
        for (int t = 3; t < tpb; t += 3)
            if (g.prob[Kick][t - 3] > 0.5f && g.prob[Kick][t] < 0.5f)
                g.set (Kick, t, pd * (0.5f + 0.5f * ctx.intensity), 0.75f);
    }
    // china replaces crashes / hats at high aggression
    if (o.chinaWithAggression > 0.f && ag > 0.62f)
    {
        const float pc = o.chinaWithAggression * (ag - 0.62f) * 2.6f;
        if (ctx.phraseStart || chinaPhrase < 0 || ctx.sectionChange) chinaPhrase = rng.chance (pc) ? 1 : 0;
        const bool swap = chinaPhrase == 1;
        for (int t = 0; t < tpb; ++t)
        {
            if (swap && g.prob[Crash][t] > 0.f)
            {
                g.prob[China][t] = g.prob[Crash][t]; g.vel[China][t] = g.vel[Crash][t];
                g.prob[Crash][t] = 0.f;
            }
        }
    }
    // bashing: drive the ride/crash instead of hats when really aggressive
    if (ag > 0.78f && ctx.intensity > 0.6f && ! ctx.style->tripletGrid
        && ctx.styleId != Orchestral && ctx.styleId != Cajon && ctx.styleId != Minimal)
    {
        for (int t = 0; t < tpb; ++t)
            if (g.prob[HatClosed][t] > 0.5f)
            {
                g.prob[Ride][t] = g.prob[HatClosed][t];
                g.vel[Ride][t] = std::max (g.vel[HatClosed][t], 0.7f);
                g.prob[HatClosed][t] = 0.f;
            }
    }
    // velocity: aggression = harder hits, flatter dynamics
    const float gain = 0.86f + 0.26f * ag;
    for (int l = 0; l < NumLanes; ++l)
        for (int t = 0; t < tpb; ++t)
            if (g.prob[l][t] > 0.f)
            {
                float v = g.vel[l][t] * gain;
                if (ag > 0.5f && v > 0.4f) v = lerp (v, 1.f, (ag - 0.5f) * 0.4f);
                g.vel[l][t] = clamp01 (v);
            }
}
} // namespace hammer
