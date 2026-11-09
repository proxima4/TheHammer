#include "Conductor.h"
#include <cmath>

namespace hammer
{
Conductor::Conductor()
{
    gp.clear (48); gn.clear (48); gb.clear (48); special.clear (48);
    for (int l = 0; l < NumLanes; ++l)
        for (int t = 0; t < kMaxTicks; ++t) { U[l][t] = 0.5f; thr[l][t] = 0.5f; }
    for (int k = 0; k < 3; ++k) for (int t = 0; t < kMaxTicks; ++t) acc[k][t] = 0.f;
}

void Conductor::reset()
{
    playbook.reset();
    if (neural) neural->reset();
    prevRole = RoleGroove; prevStyle = -1; prevSection = -1; prevTpb = 0;
    smoothIntensity = -1.f;
    phraseFillBeats = midFillBeats = turnFillBeats = 0;
    phraseSolo = false; soloBars = 0; soloBarCounter = 0;
    crashNextBar = false; stopWasActive = false;
    haveLockBar = false;
}

float Conductor::sectionOffset (int section) const
{
    switch ((Section) section)
    {
        case Section::Intro: return -0.2f;
        case Section::Verse: return -0.08f;
        case Section::PreChorus: return 0.06f;
        case Section::Chorus: return 0.2f;
        case Section::Bridge: return -0.04f;
        case Section::Breakdown: return 0.05f;
        case Section::Solo: return 0.15f;
        case Section::Outro: return -0.1f;
        default: return 0.f;
    }
}

int Conductor::effectiveSection (const EngineSettings& s) const
{
    if (sectionOverride >= 0) return sectionOverride;
    return (int) s.section;
}

void Conductor::planPhrase (const EngineSettings& s, int phraseBars, int bpb)
{
    const float fl = s.fills, so = s.solos, im = s.improvise;
    auto lenFor = [&] (float amount) {
        int beats = amount < 0.33f ? 1 : (amount < 0.7f ? 2 : 4);
        if (rng.chance (im * 0.35f)) beats = rng.chance (0.5f) ? std::max (1, beats / 2) : std::min (4, beats * 2);
        return std::min (beats, bpb);
    };
    phraseFillBeats = (fl > 0.01f && rng.chance (0.2f + 0.8f * fl)) ? lenFor (fl) : 0;
    midFillBeats = (fl > 0.2f && phraseBars >= 4 && rng.chance (fl * fl * 0.6f)) ? std::min (bpb, fl > 0.8f ? 2 : 1) : 0;
    turnFillBeats = (fl > 0.3f && phraseBars >= 8 && rng.chance (fl * 0.35f)) ? 1 : 0;
    phraseSolo = so > 0.01f && rng.chance (so * 0.75f);
    soloBars = so < 0.4f ? 1 : (so < 0.75f ? 2 : 4);
    soloBars = std::min (soloBars, std::max (1, phraseBars / 2));
}

void Conductor::refreshRandomField (bool full, float improvise)
{
    const float reroll = 0.05f + improvise * 0.5f;
    const float spread = 0.15f + 0.85f * improvise;
    for (int l = 0; l < NumLanes; ++l)
        for (int t = 0; t < kMaxTicks; ++t)
        {
            if (full || rng.chance (reroll)) U[l][t] = rng.uni();
            thr[l][t] = lerp (0.5f, U[l][t], spread);
        }
}

void Conductor::realise (const LaneGrid& g, Bar& out, int tpb)
{
    out.clear();
    out.ticksPerBar = tpb;
    for (int t = 0; t < tpb; ++t)
        for (int l = 0; l < NumLanes; ++l)
        {
            const float p = g.prob[l][t];
            if (p <= 0.f || p <= thr[l][t]) continue;
            Hit h;
            h.lane = (uint8_t) l; h.tick = (uint8_t) t;
            h.velocity = std::max (0.05f, g.vel[l][t]);
            h.offsetTicks = g.offs[l][t];
            out.add (h);
        }
}

// ---------------------------------------------------------------------------
void Conductor::generateBar (const BarRequest& req, Bar& out)
{
    const EngineSettings& s = *req.settings;
    const StyleDef& st = StyleLibrary::get (s.style);
    const int bpb = std::max (1, std::min (kMaxBeatsPerBar, req.beatsPerBar));
    const int tpb = bpb * kTicksPerBeat;
    const int phraseBars = std::max (1, s.phraseBars);
    const int barInPhrase = (int) (((req.barIndex % phraseBars) + phraseBars) % phraseBars);
    const bool phraseStart = barInPhrase == 0;
    const bool styleChanged = s.style != prevStyle;
    if (styleChanged && neural) neural->onStyleChange();
    if (tpb != prevTpb) { haveLockBar = false; }

    // ---- listening
    ListeningFeatures lf;
    if (req.listening)
    {
        lf = req.listening->features (req.nowPpq);
        if (lf.hasData) req.listening->predictAccents (req.barIndex, acc);
    }
    if (! lf.hasData)
        for (int k = 0; k < 3; ++k) for (int t = 0; t < kMaxTicks; ++t) acc[k][t] = 0.f;

    // ---- section & intensity
    const int section = effectiveSection (s);
    const bool sectionChanged = section != prevSection && prevSection >= 0;
    const bool autoChange = section == (int) Section::Auto && lf.hasData && lf.novelty > 0.5f
                            && (phraseStart || barInPhrase % 4 == 0);
    float target = clamp01 (s.intensity + sectionOffset (section));
    if (lf.hasData)
    {
        const float dynFollow = 0.15f + 0.55f * s.follow;
        target = clamp01 (lerp (target, 0.1f + 0.9f * lf.energy, dynFollow * 0.6f) + 0.08f * lf.energyTrend * s.follow);
    }
    if (smoothIntensity < 0.f || sectionChanged || autoChange || styleChanged) smoothIntensity = target;
    else smoothIntensity = 0.55f * smoothIntensity + 0.45f * target;
    const float eff = smoothIntensity;

    // ---- feel
    bool half = false, dbl = false;
    switch (s.feel)
    {
        case Feel::Half: half = true; break;
        case Feel::Double: dbl = true; break;
        case Feel::Auto:
            if (req.bpm > st.maxBpm) half = true;
            else if (req.bpm < st.minBpm) dbl = true;
            break;
        default: break;
    }
    if (halfToggle) { half = ! half; dbl = false; }

    // ---- phrase planning
    if (phraseStart || styleChanged || sectionChanged) planPhrase (s, phraseBars, bpb);
    refreshRandomField (phraseStart || styleChanged || tpb != prevTpb, s.improvise);

    // ---- bar role
    int role = RoleGroove;
    int fillBeats = 0;
    const bool stop = stopHeld || section == (int) Section::Stop;
    const bool soloSection = soloHeld || section == (int) Section::Solo;
    if (stop) role = RoleStop;
    else if (soloSection) role = RoleSolo;
    else if (phraseSolo && barInPhrase >= phraseBars - soloBars) role = RoleSolo;
    else if (barInPhrase == phraseBars - 1 && phraseFillBeats > 0) { role = RoleFill; fillBeats = phraseFillBeats; }
    else if (phraseBars >= 4 && barInPhrase == phraseBars / 2 - 1 && midFillBeats > 0) { role = RoleFill; fillBeats = midFillBeats; }
    else if (phraseBars >= 8 && (barInPhrase % 4) == 3 && turnFillBeats > 0) { role = RoleFill; fillBeats = turnFillBeats; }
    if (fillRequested && role == RoleGroove) { role = RoleFill; fillBeats = std::min (bpb, std::max (1, (int) std::lround (1 + s.fills * 3))); }
    if (section == (int) Section::Outro && barInPhrase == phraseBars - 1 && role == RoleGroove) { role = RoleFill; fillBeats = std::min (bpb, 2); }
    fillRequested = false;
    if (role == RoleSolo) soloBarCounter = (prevRole == RoleSolo) ? soloBarCounter + 1 : 0;

    // ---- context
    BrainContext ctx;
    ctx.styleId = s.style; ctx.style = &st;
    ctx.ticksPerBar = tpb; ctx.beatsPerBar = bpb; ctx.barIndex = req.barIndex;
    ctx.barInPhrase = barInPhrase; ctx.phraseBars = phraseBars;
    ctx.intensity = eff; ctx.aggression = s.aggression; ctx.follow = s.follow; ctx.improvise = s.improvise;
    ctx.bpm = req.bpm; ctx.halfTime = half; ctx.doubleTime = dbl;
    ctx.phraseStart = phraseStart; ctx.sectionChange = sectionChanged || autoChange;
    ctx.forceHalfTimeGroove = section == (int) Section::Breakdown;
    ctx.accents = acc; ctx.lf = lf;
    const int fillStart = tpb - fillBeats * kTicksPerBeat;
    ctx.fillFromTick = role == RoleFill ? fillStart : (role == RoleSolo ? 0 : -1);

    // ---- STOP: silence (one stop hit on the first stopped bar)
    if (role == RoleStop)
    {
        out.clear(); out.ticksPerBar = tpb; out.beatsPerBar = bpb; out.role = RoleStop;
        if (! stopWasActive && prevRole != RoleStop)
        {
            Hit h; h.lane = Crash; h.tick = 0; h.velocity = 0.9f; out.add (h);
            h.lane = Kick; out.add (h);
        }
        stopWasActive = true;
        prevRole = RoleStop; prevStyle = s.style; prevSection = section; prevTpb = tpb;
        status.role = RoleStop;
        return;
    }
    stopWasActive = false;

    // ---- lock: replay the stored groove bar
    if (s.lockGroove && haveLockBar && role == RoleGroove && ! styleChanged)
    {
        out = lockBar;
        prevRole = role; prevStyle = s.style; prevSection = section; prevTpb = tpb;
        status.role = role;
        return;
    }

    // ---- the two brains
    const float b = clamp01 (s.brain);
    const bool haveModel = neural != nullptr && neural->hasModel();
    if (b < 0.999f) playbook.generate (ctx, gp, rng); else gp.clear (tpb);
    if (b > 0.001f && neural != nullptr)
    {
        BrainContext nctx = ctx;
        if (role == RoleSolo) nctx.intensity = std::max (eff, 0.85f);
        neural->generate (nctx, gn, rng, thr, 0.6f + 0.9f * s.improvise);
    }
    else gn.clear (tpb);

    gb.clear (tpb);
    for (int l = 0; l < NumLanes; ++l)
        for (int t = 0; t < tpb; ++t)
        {
            gb.prob[l][t] = (1.f - b) * gp.prob[l][t] + b * gn.prob[l][t];
            const float wp = (1.f - b) * gp.prob[l][t], wn = b * gn.prob[l][t];
            const float ws = wp + wn;
            gb.vel[l][t] = ws > 0.f ? (wp * gp.vel[l][t] + wn * gn.vel[l][t]) / ws : 0.f;
            gb.offs[l][t] = ws > 0.f ? (wn * gn.offs[l][t]) / ws : 0.f;
        }

    // ---- fills & solos
    FillParams fp;
    fp.styleId = s.style; fp.style = &st; fp.ticksPerBar = tpb;
    fp.intensity = eff; fp.aggression = s.aggression; fp.improvise = s.improvise; fp.bpm = req.bpm;
    if (role == RoleFill || role == RoleSolo)
    {
        special.clear (tpb);
        FillType ft = FillTomRun;
        int from = 0;
        if (role == RoleFill)
        {
            ft = FillSoloGenerator::chooseFill (st, rng, s.aggression);
            fp.startTick = fillStart; fp.endTick = tpb;
            FillSoloGenerator::fill (ft, fp, special, rng);
            from = fillStart;
        }
        else
        {
            fp.startTick = 0; fp.endTick = tpb;
            const int total = (soloHeld || section == (int) Section::Solo) ? 1000 : soloBars;
            FillSoloGenerator::solo (fp, soloBarCounter % std::max (1, std::min (total, 1000)), total, special, rng);
            ft = FillTomRun;
        }
        const float nb = haveModel ? b : 0.f; // the neural brain plays its own fills when trained
        for (int l = 0; l < NumLanes; ++l)
        {
            const bool keep = role == RoleFill && FillSoloGenerator::keepsLane (ft, l);
            for (int t = from; t < tpb; ++t)
            {
                if (keep && special.prob[l][t] <= 0.f) continue;
                const float baseP = keep ? gb.prob[l][t] : 0.f;
                const float procP = special.prob[l][t];
                const float neuP = gn.prob[l][t];
                gb.prob[l][t] = std::max (baseP, (1.f - nb) * procP + nb * neuP);
                if (procP > 0.f || neuP > 0.f)
                {
                    const float wp = (1.f - nb) * procP, wn = nb * neuP;
                    gb.vel[l][t] = (wp + wn) > 0.f ? (wp * special.vel[l][t] + wn * gn.vel[l][t]) / (wp + wn) : gb.vel[l][t];
                }
            }
        }
    }

    // ---- crash on the "one" after a fill / solo / section change / request
    const bool afterSpecial = prevRole == RoleFill || prevRole == RoleSolo;
    if ((afterSpecial || sectionChanged || autoChange || crashRequested || crashNextBar) && role != RoleSolo)
    {
        const float pc = (crashRequested || crashNextBar) ? 1.f
                         : (sectionChanged || autoChange) ? 0.95f
                         : st.crashAfterFill * (0.55f + 0.45f * s.aggression) * (afterSpecial ? 1.f : 0.f);
        if (rng.chance (pc))
        {
            int lane = Crash;
            if ((s.style == Metalcore || s.style == DeathMetal) && s.aggression > 0.7f && rng.chance (0.5f)) lane = China;
            if (s.style == Jazz || s.style == FusionJazz) lane = rng.chance (0.5f) ? Crash2 : Crash;
            if (s.style == Minimal) lane = HatOpen;
            gb.prob[lane][0] = 1.f; gb.vel[lane][0] = 0.85f + 0.15f * s.aggression;
            gb.prob[Kick][0] = 1.f; gb.vel[Kick][0] = std::max (gb.vel[Kick][0], 0.9f);
            // the crash replaces the hat/ride on the one
            gb.prob[HatClosed][0] = 0.f; gb.prob[Ride][0] = std::min (gb.prob[Ride][0], 0.f);
            thr[lane][0] = 0.f; thr[Kick][0] = 0.f;
        }
    }
    crashRequested = false; crashNextBar = false;

    // ---- realise, play like a human
    realise (gb, out, tpb);
    out.beatsPerBar = bpb;
    out.role = role;
    if (! req.machineKit && ! req.ensembleKit) enforceLimbs (out, req, st, s.aggression);
    humanise (out, req, st, eff);

    if (role == RoleGroove) { lockBar = out; haveLockBar = true; }

    // ---- status for the GUI
    status.style = s.style; status.groove = playbook.currentGrooveIndex(); status.role = role;
    status.section = section; status.barInPhrase = barInPhrase;
    status.feel = half ? 1 : (dbl ? 3 : 2);
    status.intensity = eff; status.energy = lf.energy; status.novelty = lf.novelty;
    status.neuralModel = haveModel; status.listening = lf.hasData; status.bar = req.barIndex;

    prevRole = role; prevStyle = s.style; prevSection = section; prevTpb = tpb;
}

// ---------------------------------------------------------------------------
// Two hands, two feet. Hits are processed in time order; each hand needs a
// minimum gap between strokes; more important strokes win conflicts.
// ---------------------------------------------------------------------------
static int lanePriority (int lane, float vel)
{
    switch (lane)
    {
        case Crash: case Crash2: case China: case Splash: return 9;
        case Snare: return vel > 0.5f ? 8 : 3;
        case TomHigh: case TomMid: case TomFloor: return 7;
        case Rim: return 6;
        case Ride: case RideBell: return 5;
        case HatClosed: case HatOpen: return 4;
        default: return 2;
    }
}

static bool exemptFromLimbs (int lane)
{
    // played by an imaginary percussionist / sampler pad
    return lane == Shaker || lane == Tambourine || lane == Perc || lane == Clap || lane == Cowbell;
}

void Conductor::enforceLimbs (Bar& bar, const BarRequest& req, const StyleDef& st, float aggression)
{
    const float msPerTick = 60000.f / std::max (30.f, req.bpm) / (float) kTicksPerBeat;
    const bool doubleKick = st.orn.doubleKick > 0.f && (aggression > 0.45f || req.settings->intensity > 0.6f);
    const float handGap = 44.f, ghostGap = 30.f;
    const float footGapSingle = 80.f, footGapDouble = 52.f;
    float handLast[2] = { -1e9f, -1e9f };
    float footLast[2] = { -1e9f, -1e9f };

    // sort indices by tick then priority (simple insertion sort - bars are small)
    int idx[kMaxHitsPerBar];
    for (int i = 0; i < bar.numHits; ++i) idx[i] = i;
    for (int i = 1; i < bar.numHits; ++i)
    {
        int k = idx[i], j = i - 1;
        auto key = [&] (int a) { const auto& h = bar.hits[(size_t) a]; return h.tick * 100 - lanePriority (h.lane, h.velocity); };
        while (j >= 0 && key (idx[j]) > key (k)) { idx[j + 1] = idx[j]; --j; }
        idx[j + 1] = k;
    }
    bool keep[kMaxHitsPerBar];
    for (int i = 0; i < bar.numHits; ++i) keep[i] = true;

    for (int n = 0; n < bar.numHits; ++n)
    {
        const int i = idx[n];
        const Hit& h = bar.hits[(size_t) i];
        if (exemptFromLimbs (h.lane)) continue;
        const float tm = (float) h.tick * msPerTick;
        if (h.lane == Kick)
        {
            if (doubleKick)
            {
                // two feet alternate: each foot needs its own recovery, plus a tiny gap between feet
                const int foot = footLast[0] <= footLast[1] ? 0 : 1; // the foot that rested longest
                const float sinceAny = tm - std::max (footLast[0], footLast[1]);
                if (tm - footLast[foot] >= footGapSingle && sinceAny >= footGapDouble * 0.6f) footLast[foot] = tm;
                else keep[i] = false;
            }
            else
            {
                if (tm - footLast[0] >= footGapSingle) footLast[0] = tm; else keep[i] = false;
            }
            continue;
        }
        if (h.lane == HatPedal)
        {
            if (doubleKick && tm - footLast[1] < 120.f) keep[i] = false; // left foot busy on the double pedal
            else footLast[1] = std::max (footLast[1], tm);
            continue;
        }
        const float gap = h.velocity < 0.4f ? ghostGap : handGap;
        // the hand that has rested longest goes first; a hand already striking
        // at this exact moment can't take a second stroke
        const int pref = handLast[0] <= handLast[1] ? 0 : 1;
        int hand = -1;
        for (int m : { pref, 1 - pref })
        {
            if (std::abs (handLast[m] - tm) < 0.01f) continue;
            if (tm - handLast[m] >= gap) { hand = m; break; }
        }
        if (hand < 0) { keep[i] = false; continue; }
        handLast[hand] = tm;
    }
    int w = 0;
    for (int i = 0; i < bar.numHits; ++i)
        if (keep[i]) bar.hits[(size_t) w++] = bar.hits[(size_t) i];
    bar.numHits = w;
}

// ---------------------------------------------------------------------------
void Conductor::humanise (Bar& bar, const BarRequest& req, const StyleDef& st, float eff)
{
    const EngineSettings& s = *req.settings;
    const float h = s.humanize * (req.machineKit ? 0.35f : 1.f);
    const float tickMs = 60000.f / std::max (30.f, req.bpm) / (float) kTicksPerBeat;
    const float sw8 = clamp01 (st.swing8 * s.swing);
    const float sw16 = clamp01 (st.swing16 * s.swing);
    const float dyn = s.dynamics;
    const float level = st.baseVelocity * (0.82f + 0.3f * eff) / 0.85f;

    for (int i = 0; i < bar.numHits; ++i)
    {
        Hit& x = bar.hits[(size_t) i];
        const int inBeat = x.tick % kTicksPerBeat;

        // swing
        float sw = 0.f;
        if (st.tripletGrid)
        {
            // grooves are written in triplets; swing < 100% straightens them
            if (inBeat == 8) sw = -2.f * (1.f - sw8);
            if (inBeat == 4) sw = -1.f * (1.f - sw8);
        }
        else
        {
            if (inBeat == 6) sw = 2.f * sw8;
            if (inBeat == 3 || inBeat == 9) sw = 1.f * sw16;
        }
        x.offsetTicks += sw;

        // timing feel
        const float bias = st.feel.laneBiasMs[x.lane] * h;
        const float jitter = humanRng.gauss() * st.feel.jitterMs * h * (x.velocity < 0.35f ? 1.3f : 1.f);
        x.offsetMs = std::max (-0.45f * tickMs * 3.f, std::min (0.45f * tickMs * 3.f, bias + jitter));

        // velocity
        float v = x.velocity;
        const bool timeLane = x.lane == HatClosed || x.lane == HatOpen || x.lane == Ride || x.lane == Shaker;
        if (timeLane)
        {
            if (inBeat == 0) v *= 1.f + st.feel.accentHats * 0.3f;
            else if (inBeat % 6 != 0) v *= 0.88f;
        }
        v = lerp (0.72f, v, 0.35f + 0.65f * dyn);
        v *= level;
        v += humanRng.gauss() * st.feel.velJitter * h;
        x.velocity = std::max (0.04f, std::min (1.f, v));
    }
}

// ---------------------------------------------------------------------------
bool Conductor::injectFill (Bar& bar, int fromTick, const BarRequest& req)
{
    const EngineSettings& s = *req.settings;
    const StyleDef& st = StyleLibrary::get (s.style);
    const int tpb = bar.ticksPerBar;
    // start on the next beat, at most 2 beats long (more for big fill settings)
    int start = ((fromTick + kTicksPerBeat - 1) / kTicksPerBeat) * kTicksPerBeat;
    const int maxLen = (s.fills > 0.7f ? 4 : 2) * kTicksPerBeat;
    start = std::max (start, tpb - maxLen);
    if (tpb - start < kTicksPerBeat) { fillRequested = true; return false; } // too late: next bar
    special.clear (tpb);
    FillParams fp;
    fp.styleId = s.style; fp.style = &st; fp.ticksPerBar = tpb; fp.startTick = start; fp.endTick = tpb;
    fp.intensity = std::max (0.5f, smoothIntensity); fp.aggression = s.aggression; fp.improvise = s.improvise; fp.bpm = req.bpm;
    const FillType ft = FillSoloGenerator::chooseFill (st, rng, s.aggression);
    FillSoloGenerator::fill (ft, fp, special, rng);
    // remove what the fill replaces
    int w = 0;
    for (int i = 0; i < bar.numHits; ++i)
    {
        const Hit& h = bar.hits[(size_t) i];
        if (h.tick >= start && ! FillSoloGenerator::keepsLane (ft, h.lane)) continue;
        bar.hits[(size_t) w++] = h;
    }
    bar.numHits = w;
    Bar fillBar; fillBar.ticksPerBar = tpb;
    for (int l = 0; l < NumLanes; ++l)
        for (int t = start; t < tpb; ++t)
            if (special.prob[l][t] > 0.5f)
            {
                Hit h; h.lane = (uint8_t) l; h.tick = (uint8_t) t; h.velocity = special.vel[l][t];
                fillBar.add (h);
            }
    humanise (fillBar, req, st, std::max (0.5f, smoothIntensity));
    for (int i = 0; i < fillBar.numHits; ++i) bar.add (fillBar.hits[(size_t) i]);
    bar.role = RoleFill;
    prevRole = RoleFill;
    crashNextBar = true;
    return true;
}
} // namespace hammer
