#include "FillSoloGenerator.h"
#include <cmath>

namespace hammer
{
FillType FillSoloGenerator::chooseFill (const StyleDef& s, Rng& rng, float aggression)
{
    float w[NumFillTypes]; float sum = 0.f;
    for (int i = 0; i < NumFillTypes; ++i)
    {
        w[i] = s.fillVocab[i];
        if (i == FillBlast) w[i] *= (0.3f + 1.4f * aggression);
        if (i == FillKickSnare) w[i] *= (0.5f + aggression);
        sum += w[i];
    }
    if (sum <= 0.f) return FillTomRun;
    float r = rng.uni() * sum;
    for (int i = 0; i < NumFillTypes; ++i)
    {
        r -= w[i];
        if (r <= 0.f) return (FillType) i;
    }
    return FillTomRun;
}

bool FillSoloGenerator::keepsLane (FillType type, int lane)
{
    switch (type)
    {
        case FillJazzComp: return lane == Ride || lane == HatPedal || lane == RideBell;
        case FillBlast:
        case FillKickSnare:
        case FillBonhamTriplet: return lane == HatPedal;
        case FillOrchestral: return false;
        case FillMinimal: return lane == Kick || lane == HatClosed || lane == Shaker || lane == Clap;
        default: return lane == Kick || lane == HatPedal || lane == Shaker || lane == Tambourine;
    }
}

static int stepFor (const FillParams& p, Rng& rng)
{
    const bool trip = p.style->tripletGrid;
    // fast tempos -> coarser subdivisions (a human can't play 16ths at 260 bpm with one hand... both hands can)
    const float sixteenthMs = 60000.f / std::max (40.f, p.bpm) / 4.f;
    if (trip) return (p.intensity > 0.7f && sixteenthMs > 70.f && rng.chance (0.4f)) ? 2 : 4;
    if (p.intensity < 0.3f && rng.chance (0.6f)) return 6;   // 8th-note fill
    if (sixteenthMs < 55.f) return 6;                         // too fast for 16th tom runs
    return 3;
}

void FillSoloGenerator::fill (FillType type, const FillParams& p, LaneGrid& g, Rng& rng)
{
    const int a = std::max (0, p.startTick), b = std::min (p.endTick, p.ticksPerBar);
    if (b <= a) return;
    const int len = b - a;
    const float im = p.improvise, in = p.intensity, ag = p.aggression;
    auto put = [&] (int lane, int t, float v) { if (t >= a && t < b) { g.prob[lane][t] = 1.f; g.vel[lane][t] = clamp01 (v); } };
    auto ramp = [&] (int t) { return 0.55f + 0.45f * (float) (t - a) / (float) std::max (1, len); };

    switch (type)
    {
        case FillTomRun:
        {
            const int st = stepFor (p, rng);
            const int n = len / st;
            static const int seqA[4] = { Snare, TomHigh, TomMid, TomFloor };
            const bool reverse = rng.chance (0.15f * im);
            for (int i = 0; i < n; ++i)
            {
                const int t = a + i * st;
                if (i > 0 && rng.chance (0.12f * im)) continue; // breathing space
                int q = (i * 4) / std::max (1, n);
                if (reverse) q = 3 - q;
                if (rng.chance (0.18f * im)) q = rng.below (4);
                const int lane = seqA[q];
                float v = ramp (t) * (0.85f + 0.2f * ag);
                if ((i % 4) == 0) v += 0.08f;
                put (lane, t, v);
                if (t % kTicksPerBeat == 0 && in > 0.45f) put (Kick, t, 0.8f);
            }
            break;
        }
        case FillSnareBuild:
        {
            const int st = (in > 0.6f && len <= 24) ? 3 : stepFor (p, rng);
            for (int t = a; t < b; t += st)
            {
                put (Snare, t, 0.35f + 0.65f * (float) (t - a) / (float) len);
                if (t % kTicksPerBeat == 0) put (Kick, t, 0.75f);
            }
            if (rng.chance (0.4f) && b - 3 >= a) { put (TomFloor, b - 3, 1.f); put (Kick, b - 3, 0.9f); }
            break;
        }
        case FillLinear:
        {
            // never two limbs at once - classic funk/fusion vocabulary
            static const int cells[4][4] = {
                { Snare, TomHigh, Kick, TomMid },
                { Snare, Snare, Kick, TomFloor },
                { TomHigh, Snare, Kick, Kick },
                { Snare, TomMid, TomFloor, Kick } };
            int c = rng.below (4);
            int k = 0;
            for (int t = a; t < b; t += 3, ++k)
            {
                if (k % 4 == 0 && rng.chance (0.5f * im)) c = rng.below (4);
                const int lane = cells[c][k % 4];
                const bool accent = (k % 3) == 0; // displaced accents in groups of 3
                put (lane, t, (accent ? 0.9f : 0.55f) * ramp (t) + 0.1f);
            }
            break;
        }
        case FillTriplet:
        {
            const int st = stepFor (p, rng);
            static const int cells[3][3] = { { Snare, TomHigh, Kick }, { Snare, Snare, Kick }, { TomHigh, TomMid, TomFloor } };
            int k = 0;
            for (int t = a; t < b; t += st, ++k)
            {
                const int cell = ((t - a) / kTicksPerBeat + (rng.chance (im * 0.3f) ? 1 : 0)) % 3;
                const int lane = cells[cell][k % 3];
                put (lane, t, 0.55f + 0.4f * ramp (t) + ((k % 3) == 0 ? 0.1f : 0.f));
            }
            break;
        }
        case FillBlast:
        {
            int k = 0;
            static const int toms[4] = { Snare, TomHigh, TomMid, TomFloor };
            for (int t = a; t < b; t += 3, ++k)
            {
                put (Kick, t, 0.9f);
                const int q = (k * 4) / std::max (1, len / 3);
                put (toms[std::min (3, q)], t, 0.85f + 0.15f * ag);
            }
            break;
        }
        case FillJazzComp:
        {
            // conversational: syncopated snare/kick hits on triplet partials, a tom or two
            for (int t = a; t < b; t += 4)
            {
                if (rng.chance (0.35f + 0.35f * in)) put (Snare, t, 0.35f + 0.5f * rng.uni());
                if (rng.chance (0.12f + 0.2f * in)) put (Kick, t, 0.6f + 0.3f * in);
                if (rng.chance (0.08f * (1.f + im))) put (rng.chance (0.5f) ? TomHigh : TomFloor, t, 0.6f);
            }
            // set-up "bomb" into the next downbeat
            if (b - 4 >= a) { put (Snare, b - 4, 0.85f); put (Kick, b - 4, 0.85f); }
            break;
        }
        case FillCajon:
        {
            for (int t = a; t < b; t += 3)
            {
                const bool beat = t % kTicksPerBeat == 0;
                if (beat) put (Kick, t, 0.75f);
                else put ((t / 3) % 2 ? Snare : Rim, t, 0.35f + 0.6f * ramp (t));
                if (in > 0.6f && rng.chance (0.2f * im)) put (TomHigh, t, 0.6f);
            }
            put (Snare, b - 3, 1.f);
            break;
        }
        case FillOrchestral:
        {
            // timpani roll crescendo + suspended cymbal swell, bass drum to land
            for (int t = a; t < b; t += 2)
            {
                const float r = (float) (t - a) / (float) len;
                put (TomFloor, t, 0.25f + 0.7f * r);
                if (t % 4 == 0 && in > 0.4f) put (Ride, t, 0.15f + 0.6f * r);
                if (in > 0.75f && t % 6 == 0) put (Snare, t, 0.3f + 0.6f * r);
            }
            put (Kick, b - 2, 0.95f);
            break;
        }
        case FillMinimal:
        {
            const int n = 2 + rng.below (3);
            for (int i = 0; i < n; ++i)
            {
                const int t = b - (i + 1) * 3;
                const int choices[4] = { Rim, Perc, HatOpen, Clap };
                put (choices[rng.below (4)], t, 0.6f + 0.3f * rng.uni());
            }
            if (in > 0.75f) for (int t = std::max (a, b - 12); t < b; t += 3) put (Snare, t, 0.4f + 0.5f * ramp (t));
            break;
        }
        case FillBonhamTriplet:
        {
            // 16th-note triplets in groups of three: Right hand (tom) - Left hand - Kick,
            // travelling down the kit, kick accented to pull into the next group.
            static const int rightHand[4] = { TomHigh, TomHigh, TomMid, TomFloor };
            static const int leftHand[4] = { Snare, TomHigh, TomMid, TomFloor };
            const int nGroups = std::max (1, len / 6);
            for (int gi = 0; gi < nGroups; ++gi)
            {
                const int t = a + gi * 6;
                const int stage = std::min (3, (gi * 4) / nGroups);
                const float r = ramp (t);
                put (rightHand[stage], t, 0.7f + 0.25f * r);
                put (leftHand[stage], t + 2, 0.6f + 0.25f * r);
                put (Kick, t + 4, 0.85f + 0.15f * r);
            }
            break;
        }
        case FillKickSnare:
        default:
        {
            const int st = len >= 24 ? 6 : 3;
            for (int t = a; t < b; t += st)
            {
                put (Kick, t, 0.9f);
                put (Snare, t, 0.75f + 0.25f * ramp (t));
                if (t == a) put (Crash, t, 0.85f);
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Solo: motif development. Picks a 1-beat cell, repeats/varies/displaces it,
// density grows through the solo, last bar ends with a big unison hit.
// ---------------------------------------------------------------------------
void FillSoloGenerator::solo (const FillParams& p, int barInSolo, int soloBars, LaneGrid& g, Rng& rng)
{
    const int tpb = p.ticksPerBar;
    const bool trip = p.style->tripletGrid;
    const int st = trip ? 4 : 3;
    const int perBeat = kTicksPerBeat / st;
    const float progress = soloBars > 1 ? (float) barInSolo / (float) (soloBars - 1) : 1.f;
    const float density = clamp01 (0.35f + 0.4f * p.intensity + 0.3f * progress);

    int palette[6] = { Snare, TomHigh, TomMid, TomFloor, Kick, Snare };
    if (p.styleId == Cajon) { int c[6] = { Snare, Rim, Kick, TomHigh, Snare, Rim }; std::copy (c, c + 6, palette); }
    if (p.styleId == Orchestral) { int c[6] = { TomFloor, TomMid, TomHigh, Kick, Snare, TomMid }; std::copy (c, c + 6, palette); }
    if (p.styleId == Minimal) { int c[6] = { Rim, Perc, Clap, Kick, HatOpen, TomHigh }; std::copy (c, c + 6, palette); }

    // motif = per-step lane choices for one beat
    int motif[6];
    for (int i = 0; i < perBeat; ++i) motif[i] = palette[rng.below (6)];
    bool motifRest[6];
    for (int i = 0; i < perBeat; ++i) motifRest[i] = rng.chance (1.f - density);
    motifRest[0] = false;

    const int beats = tpb / kTicksPerBeat;
    for (int bt = 0; bt < beats; ++bt)
    {
        // every other beat: develop the motif (rotate, transpose around the kit)
        const bool develop = (bt % 2 == 1) || rng.chance (p.improvise * 0.4f);
        const int rot = develop ? 1 + rng.below (std::max (1, perBeat - 1)) : 0;
        const int shift = develop ? rng.below (3) : 0;
        for (int i = 0; i < perBeat; ++i)
        {
            const int src = (i + rot) % perBeat;
            if (motifRest[src] && ! (bt == beats - 1 && progress > 0.9f)) continue;
            int lane = motif[src];
            if (shift && lane != Kick)
            {
                int idx = 0;
                for (int k = 0; k < 6; ++k) if (palette[k] == lane) { idx = k; break; }
                lane = palette[(idx + shift) % 4];
            }
            const int t = bt * kTicksPerBeat + i * st;
            const bool accent = i == 0 || (develop && i == perBeat - 1);
            g.prob[lane][t] = 1.f;
            g.vel[lane][t] = clamp01 ((accent ? 0.9f : 0.55f) + 0.15f * progress + 0.1f * p.aggression);
        }
        // feet keep time underneath
        if (trip && p.styleId != Cajon) { g.prob[HatPedal][bt * kTicksPerBeat] = (bt % 2) ? 1.f : 0.f; g.vel[HatPedal][bt * kTicksPerBeat] = 0.6f; }
        else if (p.intensity > 0.55f) { g.prob[Kick][bt * kTicksPerBeat] = std::max (g.prob[Kick][bt * kTicksPerBeat], 0.9f); g.vel[Kick][bt * kTicksPerBeat] = 0.7f; }
    }
    if (barInSolo == soloBars - 1)
    {
        const int t = tpb - st;
        g.prob[Snare][t] = 1.f; g.vel[Snare][t] = 1.f;
        g.prob[Kick][t] = 1.f; g.vel[Kick][t] = 1.f;
    }
}
} // namespace hammer
