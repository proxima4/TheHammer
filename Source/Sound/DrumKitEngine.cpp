#include "DrumKitEngine.h"
#include <cmath>

namespace hammer
{
void DrumKitEngine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    groupBuf.setSize (NumGroups * 2, std::max (1, maxBlock));
    sendBuf.setSize (2, std::max (1, maxBlock));
    reverb.setSampleRate (sr);
    lastRoomSize = -1.f;
    for (auto& b : roomHp) { b.setHighpass (sr, 180.0); b.reset(); }
    for (auto& b : driveTilt) { b.setLowShelf (sr, 120.0, 2.0); b.reset(); }
    allNotesOff();
    (void) synthSpecFor (KitStudio, Kick, Rock); // build tables off the audio thread
}

void DrumKitEngine::setKit (int kitId, int voicing, SampleKitData* sk)
{
    kit = kitId; voicingId = voicing; sample = sk;
}

void DrumKitEngine::allNotesOff()
{
    for (auto& v : voices) v.active = false;
    numActive = 0;
}

void DrumKitEngine::fadeOutAll()
{
    const float k = (float) std::exp (-1.0 / (sr * 0.004)); // ~15 ms to -60 dB
    for (auto& v : voices)
        if (v.active)
        {
            v.choking = true;
            v.chokeK = k;
            if (! v.isSample) v.synth.choke (sr, 4.0);
        }
    roomFadeLen = roomFadeLeft = std::max (1, (int) (sr * 0.02));
}

DrumKitEngine::Voice* DrumKitEngine::allocVoice (int lane)
{
    // limit per lane so long cymbal tails don't eat the whole voice pool
    int laneCount = 0; Voice* oldestLane = nullptr; Voice* oldest = nullptr; Voice* freeV = nullptr;
    for (auto& v : voices)
    {
        if (! v.active) { if (! freeV) freeV = &v; continue; }
        if (! oldest || v.age < oldest->age) oldest = &v;
        if (v.lane == lane) { ++laneCount; if (! oldestLane || v.age < oldestLane->age) oldestLane = &v; }
    }
    const int laneMax = (lane == Kick || lane == Snare) ? 4 : 6;
    if (laneCount >= laneMax && oldestLane) return oldestLane;
    if (freeV) return freeV;
    return oldest;
}

void DrumKitEngine::trigger (int lane, float velocity, int offset)
{
    if (lane < 0 || lane >= NumLanes) return;
    velocity = clamp01 (velocity);
    // hi-hat choke: closed / pedal hats cut the open hat
    if (lane == HatClosed || lane == HatPedal)
        for (auto& v : voices)
            if (v.active && v.lane == HatOpen && ! v.choking)
            {
                v.choking = true;
                v.chokeK = (float) std::exp (-1.0 / (sr * 0.018));
                if (! v.isSample) v.synth.choke (sr);
            }

    const float curve = 0.9f + 1.6f * settings.velCurve;      // exponent
    const float amp = 0.06f + 0.94f * std::pow (velocity, curve);
    const float tune = std::pow (2.f, settings.tuneSemis / 12.f);
    Voice* v = allocVoice (lane);
    if (v == nullptr) return;

    // sample kit lane?
    if (sample != nullptr && sample->lanes[lane].present())
    {
        const auto& sl = sample->lanes[lane];
        const int L = (int) sl.layers.size();
        float pos = velocity * (float) (L - 1) + rng.range (-0.35f, 0.35f);
        const int li = std::max (0, std::min (L - 1, (int) std::lround (pos)));
        const auto& layer = sl.layers[(size_t) li];
        if (layer.rr.empty()) return;
        int rr = rng.below ((int) layer.rr.size());
        if ((int) layer.rr.size() > 1 && rr == rrLast[lane]) rr = (rr + 1) % (int) layer.rr.size();
        rrLast[lane] = rr;
        const float center = L > 1 ? (float) li / (float) (L - 1) : velocity;
        float g = L > 1 ? std::pow ((0.15f + velocity) / (0.15f + center), 0.8f) : amp;
        g = juce::jlimit (0.05f, 1.6f, g) * sl.gain * 1.5f; // sample kits are normalised to -0.5 dBFS peaks
        const float pan = sl.pan;
        v->isSample = true;
        v->buf = &layer.rr[(size_t) rr];
        v->pos = 0; v->rate = tune;
        v->gain = g;
        v->gl = std::cos ((pan + 1.f) * 0.25f * juce::MathConstants<float>::pi) * 1.4142f;
        v->gr = std::sin ((pan + 1.f) * 0.25f * juce::MathConstants<float>::pi) * 1.4142f;
    }
    else
    {
        // synth kits (also fills lanes a sample kit doesn't have)
        const int synthKit = (kit >= KitSample1) ? KitStudio : kit;
        const VoiceSpec& spec = synthSpecFor (synthKit, lane, voicingId);
        if (! spec.valid) return;
        v->isSample = false;
        v->synth.start (spec, velocity, tune, sr, ++seed);
        v->gain = amp * 0.56f; // level-match the modeled kits to the sample kits
        const float pan = spec.pan;
        v->gl = std::cos ((pan + 1.f) * 0.25f * juce::MathConstants<float>::pi) * 1.4142f;
        v->gr = std::sin ((pan + 1.f) * 0.25f * juce::MathConstants<float>::pi) * 1.4142f;
    }
    v->active = true;
    v->lane = lane;
    v->group = laneGroup (lane);
    v->delay = std::max (0, offset);
    v->choking = false; v->chokeEnv = 1.f;
    v->age = ++ageCounter;
    laneFlash[lane] = std::max (laneFlash[lane], velocity);
}

void DrumKitEngine::renderVoice (Voice& v, float* L, float* R, int n)
{
    int start = 0;
    if (v.delay > 0)
    {
        if (v.delay >= n) { v.delay -= n; return; }
        start = v.delay; v.delay = 0;
    }
    const float gl = v.gain * v.gl, gr = v.gain * v.gr;
    if (! v.isSample)
    {
        if (! v.synth.render (L + start, R + start, n - start, gl, gr)) v.active = false;
        return;
    }
    const auto& b = *v.buf;
    const int len = b.getNumSamples();
    const float* sl = b.getReadPointer (0);
    const float* sr2 = b.getReadPointer (1);
    for (int i = start; i < n; ++i)
    {
        const int ip = (int) v.pos;
        if (ip + 1 >= len) { v.active = false; return; }
        const float fr = (float) (v.pos - ip);
        float a = sl[ip] + (sl[ip + 1] - sl[ip]) * fr;
        float c = sr2[ip] + (sr2[ip + 1] - sr2[ip]) * fr;
        if (v.choking)
        {
            v.chokeEnv *= v.chokeK;
            a *= v.chokeEnv; c *= v.chokeEnv;
            if (v.chokeEnv < 1e-4f) { v.active = false; return; }
        }
        L[i] += a * gl; R[i] += c * gr;
        v.pos += v.rate;
    }
}

void DrumKitEngine::render (float* mainL, float* mainR, int n, float* const* gOutL, float* const* gOutR)
{
    if (n > groupBuf.getNumSamples()) { groupBuf.setSize (NumGroups * 2, n, false, false, true); sendBuf.setSize (2, n, false, false, true); }
    groupBuf.clear (0, n);
    sendBuf.clear (0, n);
    numActive = 0;
    for (auto& v : voices)
    {
        if (! v.active) continue;
        ++numActive;
        renderVoice (v, groupBuf.getWritePointer (v.group * 2), groupBuf.getWritePointer (v.group * 2 + 1), n);
    }

    static const float roomSend[NumGroups] = { 0.25f, 0.9f, 0.75f, 0.35f, 0.45f, 0.6f };
    float* sl = sendBuf.getWritePointer (0);
    float* sr2 = sendBuf.getWritePointer (1);
    for (int g = 0; g < NumGroups; ++g)
    {
        float* gl = groupBuf.getWritePointer (g * 2);
        float* gr = groupBuf.getWritePointer (g * 2 + 1);
        const float gg = settings.groupGain[g];
        const float send = settings.room * roomSend[g];
        const bool toAux = gOutL != nullptr && gOutL[g] != nullptr;
        for (int i = 0; i < n; ++i)
        {
            const float a = gl[i] * gg, b = gr[i] * gg;
            sl[i] += a * send; sr2[i] += b * send;
            if (toAux) { gOutL[g][i] += a * settings.master; gOutR[g][i] += b * settings.master; }
            else { mainL[i] += a; mainR[i] += b; }
        }
    }

    // ---- bus: glue compressor -> drive -> + room -> master
    const float thrDb = -10.f - 16.f * settings.glue;
    const float ratio = 1.f + 5.f * settings.glue;
    const float att = std::exp (-1.f / (float) (sr * 0.008)), rel = std::exp (-1.f / (float) (sr * 0.14));
    const float drv = settings.drive;
    const float pre = 1.f + drv * 6.f;
    const float norm = 1.f / std::tanh (pre * 0.7f);
    for (int i = 0; i < n; ++i)
    {
        float a = mainL[i], b = mainR[i];
        if (settings.glue > 0.001f)
        {
            const float lvl = std::max (std::abs (a), std::abs (b));
            compEnv = lvl > compEnv ? att * compEnv + (1 - att) * lvl : rel * compEnv + (1 - rel) * lvl;
            const float db = 20.f * std::log10 (compEnv + 1e-9f);
            float gr = 0.f;
            if (db > thrDb) gr = (db - thrDb) * (1.f - 1.f / ratio);
            const float g = std::pow (10.f, (-gr + gr * 0.35f * settings.glue) / 20.f); // light make-up
            a *= g; b *= g;
        }
        if (drv > 0.001f)
        {
            // asymmetric valve-ish saturation with a little low-end weight
            const float ta = driveTilt[0].process (a), tb = driveTilt[1].process (b);
            const float sa = std::tanh (pre * (ta + 0.08f * ta * ta) * 0.7f) * norm * 0.7f;
            const float sb = std::tanh (pre * (tb + 0.08f * tb * tb) * 0.7f) * norm * 0.7f;
            a = lerp (a, sa, std::min (1.f, drv * 1.5f));
            b = lerp (b, sb, std::min (1.f, drv * 1.5f));
        }
        mainL[i] = a; mainR[i] = b;
    }
    if (settings.room > 0.001f)
    {
        if (std::abs (settings.roomSize - lastRoomSize) > 0.01f)
        {
            juce::Reverb::Parameters p;
            p.roomSize = 0.25f + 0.7f * settings.roomSize;
            p.damping = 0.55f - 0.25f * settings.roomSize;
            p.wetLevel = 1.f; p.dryLevel = 0.f; p.width = 1.f; p.freezeMode = 0.f;
            reverb.setParameters (p);
            lastRoomSize = settings.roomSize;
        }
        for (int i = 0; i < n; ++i) { sl[i] = roomHp[0].process (sl[i]); sr2[i] = roomHp[1].process (sr2[i]); }
        reverb.processStereo (sl, sr2, n);
        for (int i = 0; i < n; ++i)
        {
            float g = 0.6f;
            if (roomFadeLeft > 0) { g *= (float) roomFadeLeft / (float) roomFadeLen; if (--roomFadeLeft == 0) { reverb.reset(); compEnv = 0.f; } }
            mainL[i] += sl[i] * g; mainR[i] += sr2[i] * g;
        }
    }
    else if (roomFadeLeft > 0) { roomFadeLeft = 0; reverb.reset(); compEnv = 0.f; }
    // master + safety soft-limit (transparent below -1 dBFS)
    auto soft = [] (float x) {
        const float a = std::abs (x);
        if (a <= 0.89f) return x;
        const float y = 0.89f + 0.11f * std::tanh ((a - 0.89f) / 0.11f);
        return x < 0.f ? -y : y;
    };
    for (int i = 0; i < n; ++i) { mainL[i] = soft (mainL[i] * settings.master * 0.7f); mainR[i] = soft (mainR[i] * settings.master * 0.7f); }
}
} // namespace hammer
