#include "SynthVoices.h"
#include "../Brain/StyleLibrary.h"
#include <cmath>

namespace hammer
{
static constexpr float kTwoPi = 6.28318530718f;
static const float kMetalRatios[6] = { 1.0f, 1.4827f, 1.8003f, 2.5460f, 2.6303f, 3.8967f }; // 808 square bank /205.3 Hz

static inline float coefMs (float ms, float sr) { return ms <= 0.f ? 0.f : std::exp (-1.f / (sr * ms * 0.001f)); }

void SynthVoice::start (const VoiceSpec& spec, float velocity, float tune, double sampleRate, uint32_t seed)
{
    s = spec;
    vel = clamp01 (velocity);
    sr = (float) sampleRate;
    tuneMul = tune;
    active = true; choking = false; chokeEnv = 1.f;
    rnd = seed * 2654435761u + 12345u; if (rnd == 0) rnd = 1;
    sampleCount = 0;
    samplesLeft = (int) (s.tailMs * 0.001f * sr);

    const float decMul = 1.f + s.velDecay * (vel - 0.7f);
    bodyCoef = coefMs (s.bodyMs * decMul, sr);
    body2Coef = coefMs (s.body2Ms * decMul, sr);
    noiseCoef = coefMs (s.noiseMs * decMul, sr);
    metalCoef = coefMs (s.metalMs * decMul, sr);
    clickCoef = coefMs (s.clickMs, sr);
    pitchCoef = coefMs (s.pitchMs, sr);
    noiseAttCoef = coefMs (std::max (0.01f, s.noiseAttMs), sr);
    for (int i = 0; i < s.modes; ++i) { modeCoef[i] = coefMs (s.bodyMs * s.modeDecay[i] * decMul, sr); modeEnv[i] = s.modeGain[i]; modePhase[i] = 0.f; }

    bodyEnv = s.bodyLvl; body2Env = s.body2Lvl; metalEnv = s.metalLvl; clickEnv = s.clickLvl; pitchEnv = 1.f;
    noiseEnv = s.noiseLvl; noiseAttEnv = s.noiseAttMs > 0.f ? 1.f : 0.f;
    phase = phase2 = 0.f;
    for (auto& p : metalPhase) p = 0.f;

    const float bright = 0.55f + s.velBright * (vel - 0.55f);
    nHp.setHighpass (sr, std::max (20.f, s.noiseHp), 0.7);
    nLp.setLowpass (sr, std::min (sr * 0.45f, std::max (200.f, s.noiseLp * bright * 1.2f)), 0.7);
    if (s.noiseBp > 0.f) nBp.setBandpass (sr, std::min (sr * 0.45f, s.noiseBp), s.noiseQ);
    mBp.setBandpass (sr, std::min (sr * 0.45f, s.metalBp * (0.9f + 0.2f * bright)), s.metalQ);
    mHp.setHighpass (sr, std::min (sr * 0.45f, s.metalHp), 0.7);
    for (auto* b : { &nHp, &nLp, &nBp, &mBp, &mHp }) b->reset();

    clapEnv = 0.f; burstIdx = 0; nextBurst = 0;
    burstSpacing = (int) (s.burstMs * 0.001f * sr);
}

bool SynthVoice::render (float* L, float* R, int n, float gL, float gR)
{
    if (! active) return false;
    const float f0 = s.bodyHz * tuneMul * (1.f + s.velPitch * (vel - 0.7f));
    const float f1 = (s.bodyEndHz > 0.f ? s.bodyEndHz : s.bodyHz) * tuneMul;
    const float w2 = kTwoPi * s.body2Hz * tuneMul / sr;
    float modeW[8];
    for (int i = 0; i < s.modes; ++i) modeW[i] = kTwoPi * s.bodyHz * s.modeRatio[i] * tuneMul / sr;
    float metalInc[6];
    for (int i = 0; i < 6; ++i) metalInc[i] = 205.3f * kMetalRatios[i] * s.metalBase * tuneMul / sr;
    const float clickW = kTwoPi * s.clickHz / sr;
    const float drive = s.bodyDrive, outDrive = s.outDrive;

    for (int i = 0; i < n; ++i)
    {
        float x = 0.f;
        // body with pitch envelope
        if (bodyEnv > 1e-5f)
        {
            const float f = f1 + (f0 - f1) * pitchEnv;
            pitchEnv *= pitchCoef;
            phase += f / sr; if (phase >= 1.f) phase -= 1.f;
            float b = std::sin (kTwoPi * phase);
            if (drive > 0.f) b = std::tanh (b * (1.f + drive * 3.f)) / std::tanh (1.f + drive * 3.f);
            x += b * bodyEnv;
            bodyEnv *= bodyCoef;
        }
        if (body2Env > 1e-5f)
        {
            phase2 += w2; if (phase2 > kTwoPi) phase2 -= kTwoPi;
            x += std::sin (phase2) * body2Env;
            body2Env *= body2Coef;
        }
        for (int m = 0; m < s.modes; ++m)
        {
            if (modeEnv[m] < 1e-5f) continue;
            modePhase[m] += modeW[m]; if (modePhase[m] > kTwoPi) modePhase[m] -= kTwoPi;
            x += std::sin (modePhase[m]) * modeEnv[m] * s.bodyLvl;
            modeEnv[m] *= modeCoef[m];
        }
        // noise
        if (s.noiseLvl > 0.f && (noiseEnv > 1e-5f || s.bursts > 0))
        {
            rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5;
            float nz = (float) (int32_t) rnd * (1.f / 2147483648.f);
            nz = nLp.process (nHp.process (nz));
            if (s.noiseBp > 0.f) nz = nBp.process (nz) * 2.f;
            float env = noiseEnv;
            if (s.bursts > 0)
            {
                if (burstIdx < s.bursts && sampleCount >= nextBurst)
                {
                    clapEnv = s.noiseLvl * (burstIdx == s.bursts - 1 ? 1.f : 0.85f);
                    ++burstIdx; nextBurst = sampleCount + burstSpacing;
                    if (burstIdx == s.bursts) noiseEnv = s.noiseLvl;
                }
                env = (burstIdx < s.bursts ? clapEnv : std::max (clapEnv, noiseEnv));
                clapEnv *= 0.9965f;
                if (burstIdx >= s.bursts) noiseEnv *= noiseCoef;
            }
            else
            {
                noiseEnv *= noiseCoef;
            }
            if (noiseAttEnv > 0.f) { env *= (1.f - noiseAttEnv); noiseAttEnv *= noiseAttCoef; }
            x += nz * env;
        }
        // metallic bank
        if (metalEnv > 1e-5f)
        {
            float m = 0.f;
            for (int k = 0; k < 6; ++k)
            {
                if ((s.metalMask & (1 << k)) == 0) continue;
                metalPhase[k] += metalInc[k]; if (metalPhase[k] >= 1.f) metalPhase[k] -= 1.f;
                m += metalPhase[k] < 0.5f ? 1.f : -1.f;
            }
            m = mHp.process (mBp.process (m * (s.metalMask == 0x3F ? 0.16f : 0.4f)));
            x += m * metalEnv * 2.2f;
            metalEnv *= metalCoef;
        }
        // click
        if (clickEnv > 1e-5f)
        {
            x += std::sin (clickW * (float) sampleCount) * clickEnv;
            clickEnv *= clickCoef;
        }
        if (outDrive > 0.f) x = std::tanh (x * (1.f + outDrive * 4.f)) / (1.f + outDrive);
        if (choking) { chokeEnv *= chokeRel; x *= chokeEnv; }
        x *= s.gain;
        L[i] += x * gL;
        R[i] += x * gR;
        ++sampleCount;
        if (--samplesLeft <= 0 || (choking && chokeEnv < 1e-4f)) { active = false; return false; }
    }
    // finished when every envelope is quiet
    float mx = std::max ({ bodyEnv, body2Env, metalEnv, clickEnv, noiseEnv, clapEnv });
    for (int m = 0; m < s.modes; ++m) mx = std::max (mx, modeEnv[m] * s.bodyLvl);
    if (mx < 2e-5f && (s.bursts == 0 || burstIdx >= s.bursts)) active = false;
    return active;
}

// ===========================================================================
//  Kit tables
// ===========================================================================
namespace
{
VoiceSpec body (float hz, float endHz, float pitchMs, float ms, float lvl = 1.f)
{
    VoiceSpec v; v.valid = true; v.bodyHz = hz; v.bodyEndHz = endHz; v.pitchMs = pitchMs; v.bodyMs = ms; v.bodyLvl = lvl;
    v.tailMs = std::max (200.f, ms * 6.f);
    return v;
}
VoiceSpec noiseV (float lvl, float ms, float hp, float lp, float bp = 0, float q = 1)
{
    VoiceSpec v; v.valid = true; v.noiseLvl = lvl; v.noiseMs = ms; v.noiseHp = hp; v.noiseLp = lp; v.noiseBp = bp; v.noiseQ = q;
    v.tailMs = std::max (200.f, ms * 6.f);
    return v;
}
VoiceSpec& withNoise (VoiceSpec& v, float lvl, float ms, float hp, float lp, float bp = 0, float q = 1)
{
    v.noiseLvl = lvl; v.noiseMs = ms; v.noiseHp = hp; v.noiseLp = lp; v.noiseBp = bp; v.noiseQ = q;
    v.tailMs = std::max (v.tailMs, ms * 6.f);
    return v;
}
VoiceSpec& withBody2 (VoiceSpec& v, float hz, float ms, float lvl) { v.body2Hz = hz; v.body2Ms = ms; v.body2Lvl = lvl; return v; }
VoiceSpec& withMetal (VoiceSpec& v, float lvl, float base, float ms, float hp, float bp, float q = 1.2f)
{
    v.metalLvl = lvl; v.metalBase = base; v.metalMs = ms; v.metalHp = hp; v.metalBp = bp; v.metalQ = q;
    v.tailMs = std::max (v.tailMs, ms * 6.f);
    return v;
}
VoiceSpec& withClick (VoiceSpec& v, float lvl, float ms, float hz) { v.clickLvl = lvl; v.clickMs = ms; v.clickHz = hz; return v; }
VoiceSpec& withModes (VoiceSpec& v, std::initializer_list<float> ratios, std::initializer_list<float> decays, std::initializer_list<float> gains)
{
    v.modes = 0;
    auto r = ratios.begin(); auto d = decays.begin(); auto g = gains.begin();
    for (; r != ratios.end() && v.modes < 8; ++r, ++v.modes)
    {
        v.modeRatio[v.modes] = *r;
        v.modeDecay[v.modes] = d != decays.end() ? *d++ : 0.5f;
        v.modeGain[v.modes] = g != gains.end() ? *g++ : 0.3f;
    }
    return v;
}
VoiceSpec metalOnly (float lvl, float base, float ms, float hp, float bp, float q = 1.2f)
{
    VoiceSpec v; v.valid = true; withMetal (v, lvl, base, ms, hp, bp, q); return v;
}
VoiceSpec clapV (int bursts, float spacing, float bp, float q, float tail)
{
    VoiceSpec v = noiseV (1.f, tail, 400, 9000, bp, q);
    v.bursts = bursts; v.burstMs = spacing;
    return v;
}
VoiceSpec gain (VoiceSpec v, float g, float pan = 0.f) { v.gain = g; v.pan = pan; return v; }

static const float kMembrane[5] = { 1.0f, 1.59f, 2.14f, 2.30f, 2.65f };

using KitTable = VoiceSpec[NumLanes];

void build808 (KitTable& k)
{
    { auto v = body (62, 48, 45, 480); withClick (v, 0.25f, 1.5f, 1600); v.bodyDrive = 0.15f; v.velDecay = 0.1f; k[Kick] = gain (v, 1.0f); }
    { auto v = body (185, 165, 10, 110, 0.55f); withBody2 (v, 330, 70, 0.35f); withNoise (v, 0.6f, 150, 1800, 9500); k[Snare] = gain (v, 0.85f); }
    { auto v = body (1667, 0, 1, 9, 0.5f); withBody2 (v, 455, 12, 0.7f); v.outDrive = 0.6f; k[Rim] = gain (v, 0.6f, -0.1f); }
    { auto v = metalOnly (1.f, 1.f, 42, 7000, 9500, 1.0f); k[HatClosed] = gain (v, 0.55f, 0.25f); }
    { auto v = metalOnly (1.f, 1.f, 380, 7000, 9500, 1.0f); k[HatOpen] = gain (v, 0.5f, 0.25f); }
    { auto v = metalOnly (0.8f, 1.f, 28, 7000, 9000, 1.0f); k[HatPedal] = gain (v, 0.4f, 0.25f); }
    { auto v = body (172, 160, 20, 280); withNoise (v, 0.05f, 60, 300, 3000); k[TomHigh] = gain (v, 0.8f, -0.3f); }
    { auto v = body (124, 114, 20, 330); withNoise (v, 0.05f, 60, 300, 3000); k[TomMid] = gain (v, 0.8f, 0.f); }
    { auto v = body (86, 80, 20, 380); withNoise (v, 0.05f, 60, 300, 3000); k[TomFloor] = gain (v, 0.85f, 0.3f); }
    { auto v = metalOnly (1.f, 1.f, 1200, 5000, 6500, 0.8f); k[Crash] = gain (v, 0.5f, -0.35f); }
    { auto v = metalOnly (1.f, 1.08f, 1000, 5200, 7000, 0.8f); k[Crash2] = gain (v, 0.45f, 0.4f); }
    { auto v = metalOnly (1.f, 1.2f, 900, 7000, 9500, 1.5f); k[Ride] = gain (v, 0.42f, 0.35f); }
    { auto v = metalOnly (1.f, 1.35f, 700, 4000, 5200, 2.0f); k[RideBell] = gain (v, 0.45f, 0.35f); }
    { auto v = metalOnly (1.f, 0.9f, 1100, 3500, 4500, 0.7f); v.outDrive = 0.5f; k[China] = gain (v, 0.5f, -0.4f); }
    { auto v = metalOnly (1.f, 1.3f, 400, 7000, 9000, 0.8f); k[Splash] = gain (v, 0.45f, -0.2f); }
    { auto v = clapV (4, 9.5f, 1100, 1.1f, 180); k[Clap] = gain (v, 0.75f); }
    { VoiceSpec v; v.valid = true; withMetal (v, 1.f, 1.f, 240, 500, 2640, 1.4f); v.metalMask = 0x30; k[Cowbell] = gain (v, 0.5f, 0.1f); }
    { auto v = noiseV (0.8f, 32, 5000, 16000); k[Shaker] = gain (v, 0.45f, 0.3f); }
    { auto v = noiseV (0.7f, 120, 6500, 16000); withMetal (v, 0.5f, 2.4f, 140, 7000, 9500); v.bursts = 2; v.burstMs = 8; k[Tambourine] = gain (v, 0.45f, -0.3f); }
    { auto v = body (2500, 0, 1, 25, 0.8f); k[Perc] = gain (v, 0.55f, 0.2f); }
}

void build909 (KitTable& k)
{
    { auto v = body (230, 52, 18, 330); withClick (v, 0.45f, 3.f, 3500); withNoise (v, 0.18f, 6, 2000, 12000); v.bodyDrive = 0.35f; k[Kick] = gain (v, 1.0f); }
    { auto v = body (190, 165, 12, 120, 0.55f); withBody2 (v, 345, 60, 0.3f); withNoise (v, 0.85f, 190, 1200, 13000); v.outDrive = 0.15f; k[Snare] = gain (v, 0.85f); }
    { auto v = body (1100, 0, 1, 10, 0.5f); withBody2 (v, 1800, 8, 0.4f); withNoise (v, 0.4f, 12, 2000, 12000); v.outDrive = 0.4f; k[Rim] = gain (v, 0.6f, -0.1f); }
    { auto v = metalOnly (0.8f, 1.45f, 50, 8000, 10500, 0.9f); withNoise (v, 0.35f, 45, 8000, 16000); k[HatClosed] = gain (v, 0.55f, 0.25f); }
    { auto v = metalOnly (0.8f, 1.45f, 430, 8000, 10500, 0.9f); withNoise (v, 0.35f, 400, 8000, 16000); k[HatOpen] = gain (v, 0.5f, 0.25f); }
    { auto v = metalOnly (0.7f, 1.45f, 30, 8000, 10000, 0.9f); k[HatPedal] = gain (v, 0.4f, 0.25f); }
    { auto v = body (260, 175, 60, 300); withNoise (v, 0.1f, 80, 400, 5000); k[TomHigh] = gain (v, 0.8f, -0.3f); }
    { auto v = body (200, 130, 60, 340); withNoise (v, 0.1f, 80, 400, 5000); k[TomMid] = gain (v, 0.8f, 0.f); }
    { auto v = body (160, 95, 60, 380); withNoise (v, 0.1f, 80, 400, 5000); k[TomFloor] = gain (v, 0.85f, 0.3f); }
    { auto v = noiseV (0.9f, 1500, 5000, 16000); withMetal (v, 0.5f, 1.7f, 1600, 6000, 8000, 0.7f); k[Crash] = gain (v, 0.45f, -0.35f); }
    { auto v = noiseV (0.9f, 1300, 5500, 16000); withMetal (v, 0.5f, 1.85f, 1400, 6000, 8500, 0.7f); k[Crash2] = gain (v, 0.42f, 0.4f); }
    { auto v = metalOnly (0.9f, 2.1f, 1400, 7000, 11000, 1.3f); withNoise (v, 0.2f, 1200, 7000, 16000); k[Ride] = gain (v, 0.4f, 0.35f); }
    { auto v = metalOnly (1.f, 1.6f, 900, 3500, 5000, 2.2f); k[RideBell] = gain (v, 0.42f, 0.35f); }
    { auto v = noiseV (0.8f, 1200, 3000, 10000); withMetal (v, 0.6f, 1.2f, 1200, 3500, 5000, 0.7f); v.outDrive = 0.5f; k[China] = gain (v, 0.45f, -0.4f); }
    { auto v = noiseV (0.8f, 450, 6000, 16000); withMetal (v, 0.5f, 2.2f, 450, 7000, 9000); k[Splash] = gain (v, 0.42f, -0.2f); }
    { auto v = clapV (4, 8.f, 1300, 1.3f, 220); k[Clap] = gain (v, 0.75f); }
    { VoiceSpec v; v.valid = true; withMetal (v, 1.f, 1.f, 220, 500, 2700, 1.4f); v.metalMask = 0x30; k[Cowbell] = gain (v, 0.5f, 0.1f); }
    { auto v = noiseV (0.8f, 35, 5500, 16000); k[Shaker] = gain (v, 0.45f, 0.3f); }
    { auto v = noiseV (0.7f, 140, 6500, 16000); withMetal (v, 0.5f, 2.6f, 150, 7000, 10000); v.bursts = 2; v.burstMs = 8; k[Tambourine] = gain (v, 0.45f, -0.3f); }
    { auto v = body (1900, 0, 1, 30, 0.8f); k[Perc] = gain (v, 0.55f, 0.2f); }
}

void buildCR78 (KitTable& k)
{
    { auto v = body (64, 58, 15, 160); k[Kick] = gain (v, 1.0f); }
    { auto v = body (220, 210, 5, 40, 0.4f); withNoise (v, 0.7f, 90, 2500, 8000); k[Snare] = gain (v, 0.75f); }
    { auto v = body (1000, 0, 1, 14, 0.7f); k[Rim] = gain (v, 0.5f, -0.1f); }
    { auto v = metalOnly (0.9f, 1.6f, 24, 9000, 10500, 1.2f); k[HatClosed] = gain (v, 0.5f, 0.25f); }
    { auto v = metalOnly (0.9f, 1.6f, 220, 9000, 10500, 1.2f); k[HatOpen] = gain (v, 0.45f, 0.25f); }
    { auto v = metalOnly (0.7f, 1.6f, 18, 9000, 10500, 1.2f); k[HatPedal] = gain (v, 0.35f, 0.25f); }
    { auto v = body (420, 400, 5, 110); k[TomHigh] = gain (v, 0.6f, -0.3f); }   // bongo hi
    { auto v = body (300, 285, 5, 130); k[TomMid] = gain (v, 0.6f, 0.f); }      // bongo lo
    { auto v = body (180, 170, 8, 200); k[TomFloor] = gain (v, 0.7f, 0.3f); }   // conga
    { auto v = metalOnly (0.9f, 1.3f, 700, 7000, 8500, 0.8f); k[Crash] = gain (v, 0.42f, -0.35f); }
    { auto v = metalOnly (0.9f, 1.4f, 600, 7000, 9000, 0.8f); k[Crash2] = gain (v, 0.4f, 0.4f); }
    { auto v = metalOnly (0.9f, 1.5f, 650, 7500, 9500, 1.4f); k[Ride] = gain (v, 0.38f, 0.35f); }
    { auto v = metalOnly (0.9f, 1.7f, 500, 4000, 5000, 2.0f); k[RideBell] = gain (v, 0.4f, 0.35f); }
    { auto v = metalOnly (0.9f, 1.1f, 700, 4000, 5000, 0.8f); v.outDrive = 0.4f; k[China] = gain (v, 0.42f, -0.4f); }
    { auto v = metalOnly (0.9f, 1.8f, 300, 8000, 9500, 0.8f); k[Splash] = gain (v, 0.4f, -0.2f); }
    { auto v = clapV (3, 12.f, 1200, 1.f, 140); k[Clap] = gain (v, 0.6f); }
    { VoiceSpec v; v.valid = true; withMetal (v, 1.f, 1.07f, 160, 600, 2900, 1.5f); v.metalMask = 0x30; k[Cowbell] = gain (v, 0.45f, 0.1f); }
    { auto v = noiseV (0.8f, 25, 6000, 16000); k[Shaker] = gain (v, 0.42f, 0.3f); }
    { auto v = noiseV (0.7f, 140, 7000, 16000); withMetal (v, 0.5f, 2.5f, 140, 7000, 9500); v.bursts = 2; v.burstMs = 15; k[Tambourine] = gain (v, 0.42f, -0.3f); }
    { auto v = clapV (6, 12.f, 3000, 2.f, 40); k[Perc] = gain (v, 0.45f, 0.2f); } // guiro
}

void buildLinn (KitTable& k)
{
    { auto v = body (120, 55, 25, 230); withClick (v, 0.35f, 2.f, 2500); v.bodyDrive = 0.25f; k[Kick] = gain (v, 1.0f); }
    { auto v = body (200, 180, 10, 90, 0.5f); withBody2 (v, 290, 50, 0.2f); withNoise (v, 0.9f, 230, 900, 11000); v.outDrive = 0.2f; v.tailMs = 300; k[Snare] = gain (v, 0.85f); }
    { auto v = body (800, 0, 1, 18, 0.6f); withNoise (v, 0.3f, 15, 2000, 10000); k[Rim] = gain (v, 0.55f, -0.1f); }
    { auto v = noiseV (0.8f, 40, 7500, 16000, 10000, 0.8f); withMetal (v, 0.3f, 2.f, 40, 8000, 10000); k[HatClosed] = gain (v, 0.55f, 0.25f); }
    { auto v = noiseV (0.8f, 300, 7500, 16000, 10000, 0.8f); withMetal (v, 0.3f, 2.f, 300, 8000, 10000); k[HatOpen] = gain (v, 0.5f, 0.25f); }
    { auto v = noiseV (0.6f, 28, 6000, 14000); k[HatPedal] = gain (v, 0.4f, 0.25f); }
    { auto v = body (210, 160, 40, 280); withNoise (v, 0.12f, 40, 500, 6000); k[TomHigh] = gain (v, 0.8f, -0.3f); }
    { auto v = body (160, 120, 40, 320); withNoise (v, 0.12f, 40, 500, 6000); k[TomMid] = gain (v, 0.8f, 0.f); }
    { auto v = body (120, 88, 40, 380); withNoise (v, 0.12f, 40, 500, 6000); k[TomFloor] = gain (v, 0.85f, 0.3f); }
    { auto v = noiseV (0.9f, 1500, 4000, 16000); withMetal (v, 0.4f, 2.2f, 1500, 5000, 7500, 0.7f); k[Crash] = gain (v, 0.45f, -0.35f); }
    { auto v = noiseV (0.9f, 1300, 4500, 16000); withMetal (v, 0.4f, 2.4f, 1300, 5500, 8000, 0.7f); k[Crash2] = gain (v, 0.42f, 0.4f); }
    { auto v = metalOnly (0.9f, 2.2f, 1200, 7000, 9500, 1.4f); withNoise (v, 0.2f, 1100, 7000, 16000); k[Ride] = gain (v, 0.4f, 0.35f); }
    { auto v = metalOnly (1.f, 1.7f, 800, 3500, 4800, 2.2f); k[RideBell] = gain (v, 0.42f, 0.35f); }
    { auto v = noiseV (0.8f, 1100, 3000, 10000); withMetal (v, 0.6f, 1.3f, 1100, 3500, 5000, 0.7f); v.outDrive = 0.5f; k[China] = gain (v, 0.45f, -0.4f); }
    { auto v = noiseV (0.8f, 400, 6000, 16000); withMetal (v, 0.5f, 2.6f, 400, 7000, 9000); k[Splash] = gain (v, 0.42f, -0.2f); }
    { auto v = clapV (3, 11.f, 1500, 1.f, 250); k[Clap] = gain (v, 0.75f); }
    { VoiceSpec v; v.valid = true; withMetal (v, 1.f, 1.f, 260, 500, 2700, 1.4f); v.metalMask = 0x30; k[Cowbell] = gain (v, 0.5f, 0.1f); }
    { auto v = noiseV (0.8f, 40, 4500, 14000); k[Shaker] = gain (v, 0.45f, 0.3f); }       // cabasa
    { auto v = noiseV (0.7f, 160, 6500, 16000); withMetal (v, 0.5f, 2.5f, 160, 7000, 9500); v.bursts = 2; v.burstMs = 9; k[Tambourine] = gain (v, 0.45f, -0.3f); }
    { auto v = body (950, 0, 1, 40, 0.7f); withBody2 (v, 2100, 20, 0.4f); k[Perc] = gain (v, 0.5f, 0.2f); }
}

// Modeled acoustic studio kit, tuned for the style ("voicing")
void buildStudio (KitTable& k, int voicing)
{
    const bool jazz = voicing == Jazz || voicing == FusionJazz || voicing == Blues;
    const bool brushes = voicing == 100;
    const bool metal = voicing == Metalcore || voicing == DeathMetal;
    const bool funk = voicing == Funk || voicing == Groove;

    // KICK
    {
        float f = jazz || brushes ? 72.f : (funk ? 62.f : 55.f);
        auto v = body (f * 2.f, f, 32, jazz || brushes ? 380 : (funk ? 190 : 260));
        withNoise (v, jazz ? 0.08f : 0.16f, 14, 1500, 6000);
        if (metal) withClick (v, 0.75f, 1.3f, 5200);
        else withClick (v, jazz ? 0.08f : 0.28f, 2.f, 3200);
        v.bodyDrive = metal ? 0.35f : 0.18f;
        v.velPitch = 0.1f;
        k[Kick] = gain (v, 1.f);
    }
    // SNARE
    {
        float f = jazz ? 320.f : (funk ? 270.f : (metal ? 215.f : 190.f));
        auto v = body (f * 1.05f, f, 8, jazz ? 110 : (funk ? 90 : 140), 0.55f);
        withModes (v, { 1.59f, 2.14f, 2.30f, 2.65f }, { 0.7f, 0.5f, 0.45f, 0.4f }, { 0.55f, 0.4f, 0.32f, 0.26f });
        withNoise (v, jazz ? 0.65f : (metal ? 1.0f : 0.85f), jazz ? 150 : (funk ? 150 : 200), 1500, 12500, 5000, 0.6f);
        withClick (v, metal ? 0.35f : 0.15f, 1.5f, 3800);
        v.noiseAttMs = 0.6f;
        v.velBright = 0.8f;
        if (brushes)
        {
            v.bodyLvl = 0.22f; v.noiseLvl = 0.9f; v.noiseMs = 260; v.noiseAttMs = 4; v.noiseHp = 900; v.noiseLp = 7000; v.noiseBp = 0; v.clickLvl = 0;
        }
        k[Snare] = gain (v, 0.85f);
    }
    // SIDE STICK
    {
        auto v = body (520, 0, 1, 25, 0.6f); withBody2 (v, 1250, 18, 0.45f); withNoise (v, 0.3f, 12, 2000, 9000);
        k[Rim] = gain (v, 0.6f, -0.05f);
    }
    // HATS
    {
        auto v = noiseV (0.7f, brushes ? 70 : 55, 7000, 16000, 9500, 0.8f); withMetal (v, 0.5f, 2.2f, 50, 8000, 10000);
        k[HatClosed] = gain (v, 0.5f, 0.3f);
        auto o = noiseV (0.7f, 650, 6500, 16000, 9000, 0.7f); withMetal (o, 0.5f, 2.2f, 600, 7500, 9500);
        k[HatOpen] = gain (o, 0.45f, 0.3f);
        auto p = noiseV (0.6f, 28, 3000, 9000); withBody2 (p, 640, 8, 0.15f);
        k[HatPedal] = gain (p, 0.4f, 0.3f);
    }
    // TOMS (membrane modes)
    {
        const float hi = jazz ? 260.f : (metal ? 180.f : 196.f);
        const float mi = jazz ? 196.f : (metal ? 140.f : 147.f);
        const float lo = jazz ? 147.f : (metal ? 95.f : 98.f);
        const float f[3] = { hi, mi, lo }; const float d[3] = { 380, 450, 560 }; const float pan[3] = { -0.3f, 0.05f, 0.4f };
        const int lanes[3] = { TomHigh, TomMid, TomFloor };
        for (int i = 0; i < 3; ++i)
        {
            auto v = body (f[i] * 1.15f, f[i], 40, d[i]);
            withModes (v, { 1.59f, 2.14f, 2.65f }, { 0.6f, 0.45f, 0.35f }, { 0.35f, 0.22f, 0.15f });
            withNoise (v, 0.12f, 15, 800, 7000);
            if (metal) withClick (v, 0.3f, 1.5f, 4000);
            k[lanes[i]] = gain (v, 0.85f, pan[i]);
        }
    }
    // CYMBALS
    {
        auto c = noiseV (0.7f, 1500, 3500, 15000); withMetal (c, 0.55f, 2.6f, 1700, 4500, 7000, 0.5f); c.noiseAttMs = 2; c.tailMs = 6000;
        k[Crash] = gain (c, 0.42f, -0.4f);
        auto c2 = noiseV (0.7f, 1300, 3800, 15000); withMetal (c2, 0.55f, 2.9f, 1400, 5000, 7500, 0.5f); c2.noiseAttMs = 2; c2.tailMs = 6000;
        k[Crash2] = gain (c2, 0.4f, 0.45f);
        auto r = noiseV (0.25f, 2400, 6000, 15000); withMetal (r, 0.45f, 3.1f, 2600, 7000, 9000, 0.8f); withClick (r, 0.35f, 6.f, 4500); withBody2 (r, 4300, 400, 0.12f); r.tailMs = 7000;
        k[Ride] = gain (r, 0.38f, 0.4f);
        auto b = body (1450, 0, 1, 900, 0.5f); withModes (b, { 2.76f, 5.4f }, { 0.6f, 0.3f }, { 0.4f, 0.2f }); withMetal (b, 0.3f, 3.f, 900, 6000, 8000); b.tailMs = 5000;
        k[RideBell] = gain (b, 0.36f, 0.4f);
        auto ch = noiseV (0.6f, 1200, 2500, 9000); withMetal (ch, 0.7f, 2.f, 1300, 3000, 4800, 0.7f); ch.outDrive = 0.5f; ch.tailMs = 5000;
        k[China] = gain (ch, 0.42f, -0.45f);
        auto sp = noiseV (0.6f, 450, 5000, 16000); withMetal (sp, 0.5f, 3.5f, 500, 6000, 9000); sp.tailMs = 2500;
        k[Splash] = gain (sp, 0.38f, -0.2f);
    }
    // PERCUSSION
    { auto v = clapV (4, 10.f, 1200, 1.f, 160); k[Clap] = gain (v, 0.7f); }
    { VoiceSpec v; v.valid = true; withMetal (v, 1.f, 1.f, 300, 500, 2600, 1.4f); v.metalMask = 0x30; k[Cowbell] = gain (v, 0.45f, 0.15f); }
    { auto v = noiseV (0.8f, 40, 5500, 16000, 8000, 0.8f); v.noiseAttMs = 6; k[Shaker] = gain (v, 0.45f, 0.35f); }
    { auto v = noiseV (0.5f, 180, 7000, 16000); withMetal (v, 0.5f, 3.f, 200, 6000, 9500); v.bursts = 2; v.burstMs = 8; k[Tambourine] = gain (v, 0.42f, -0.35f); }
    { auto v = body (900, 0, 1, 40, 0.7f); withBody2 (v, 2100, 20, 0.4f); k[Perc] = gain (v, 0.5f, 0.2f); }
}

void buildCajon (KitTable& k)
{
    { auto v = body (98, 86, 15, 170); withModes (v, { 2.3f, 3.6f }, { 0.5f, 0.3f }, { 0.25f, 0.12f }); withNoise (v, 0.15f, 30, 60, 1500); k[Kick] = gain (v, 1.f); }
    { auto v = body (420, 410, 3, 60, 0.45f); withModes (v, { 1.5f, 2.1f, 2.9f, 3.7f }, { 0.8f, 0.6f, 0.5f, 0.4f }, { 0.5f, 0.4f, 0.3f, 0.2f });
      withNoise (v, 0.85f, 130, 2000, 11000, 4500, 0.6f); withClick (v, 0.2f, 1.f, 3000); v.velBright = 0.8f; k[Snare] = gain (v, 0.85f); }
    { auto v = body (620, 0, 1, 30, 0.4f); withNoise (v, 0.45f, 35, 2500, 10000, 4500, 0.7f); k[Rim] = gain (v, 0.55f, 0.f); }
    { auto v = noiseV (0.8f, 45, 5000, 16000, 8000, 0.8f); v.noiseAttMs = 8; k[HatClosed] = gain (v, 0.42f, 0.35f); k[Shaker] = gain (v, 0.42f, 0.35f); k[HatPedal] = gain (v, 0.3f, 0.35f); }
    { auto v = noiseV (0.8f, 130, 5000, 16000, 8000, 0.8f); v.noiseAttMs = 15; k[HatOpen] = gain (v, 0.42f, 0.35f); k[Ride] = gain (v, 0.38f, 0.35f); k[RideBell] = gain (v, 0.38f, 0.35f); }
    { auto v = body (430, 410, 6, 110); withModes (v, { 1.6f }, { 0.5f }, { 0.25f }); withNoise (v, 0.1f, 10, 1500, 9000); k[TomHigh] = gain (v, 0.6f, -0.4f); }   // bongo
    { auto v = body (225, 215, 8, 220); withModes (v, { 1.6f }, { 0.5f }, { 0.25f }); withNoise (v, 0.1f, 10, 1500, 9000); k[TomMid] = gain (v, 0.7f, 0.3f); }    // conga
    { auto v = body (78, 72, 15, 220); withNoise (v, 0.12f, 30, 60, 1200); k[TomFloor] = gain (v, 0.9f, 0.f); }
    { auto v = noiseV (0.6f, 220, 7000, 16000); withMetal (v, 0.6f, 3.f, 240, 6000, 9500); v.bursts = 2; v.burstMs = 7; k[Tambourine] = gain (v, 0.45f, -0.35f);
      k[Crash] = gain (v, 0.5f, -0.35f); k[Crash2] = gain (v, 0.48f, 0.35f); k[China] = gain (v, 0.5f, -0.35f); k[Splash] = gain (v, 0.42f, 0.2f); }
    { auto v = clapV (4, 10.f, 1200, 1.f, 140); k[Clap] = gain (v, 0.6f); }
    { VoiceSpec v; v.valid = true; withMetal (v, 1.f, 1.f, 300, 500, 2600, 1.4f); v.metalMask = 0x30; k[Cowbell] = gain (v, 0.42f, 0.15f); }
    { auto v = body (2500, 0, 1, 30, 0.8f); k[Perc] = gain (v, 0.5f, 0.2f); } // claves
}

void buildOrchestral (KitTable& k)
{
    { auto v = body (50, 46, 60, 1500); withModes (v, { 1.5f, 2.0f, 2.4f }, { 0.8f, 0.6f, 0.5f }, { 0.4f, 0.25f, 0.15f }); withNoise (v, 0.12f, 200, 40, 400); v.tailMs = 5000; v.velDecay = 0.4f; k[Kick] = gain (v, 1.0f); }
    { auto v = body (245, 240, 5, 90, 0.4f); withModes (v, { 1.59f, 2.14f, 2.65f }, { 0.6f, 0.5f, 0.4f }, { 0.4f, 0.3f, 0.2f }); withNoise (v, 0.9f, 160, 1800, 11500, 5500, 0.6f); v.noiseAttMs = 0.5f; k[Snare] = gain (v, 0.7f, -0.1f);
      auto tap = v; tap.noiseMs = 80; tap.bodyLvl = 0.3f; k[HatClosed] = gain (tap, 0.4f, -0.1f); }
    // timpani (D3 / A2 / E2)
    {
        const float f[3] = { 146.8f, 110.f, 82.4f }; const int lanes[3] = { TomHigh, TomMid, TomFloor }; const float pan[3] = { -0.25f, 0.1f, 0.35f };
        for (int i = 0; i < 3; ++i)
        {
            auto v = body (f[i] * 1.02f, f[i], 80, 2200);
            withModes (v, { 1.5f, 1.98f, 2.44f, 2.94f }, { 0.85f, 0.7f, 0.6f, 0.5f }, { 0.8f, 0.5f, 0.3f, 0.2f });
            withNoise (v, 0.1f, 30, 60, 1200);
            v.tailMs = 6000; v.velBright = 0.7f;
            k[lanes[i]] = gain (v, 0.8f, pan[i]);
        }
    }
    { auto v = noiseV (0.9f, 2500, 2500, 14000); withMetal (v, 0.5f, 2.3f, 2200, 3500, 6500, 0.5f); v.tailMs = 7000; k[Crash] = gain (v, 0.45f, -0.3f); k[Crash2] = gain (v, 0.42f, 0.3f); }
    { auto v = noiseV (0.5f, 2800, 4000, 14000); withMetal (v, 0.4f, 2.6f, 3000, 4500, 7000, 0.5f); v.noiseAttMs = 15; v.tailMs = 8000; k[Ride] = gain (v, 0.38f, 0.3f); k[HatOpen] = gain (v, 0.3f, 0.3f); k[RideBell] = gain (v, 0.35f, 0.3f); }
    { auto v = body (65, 64, 200, 5000); withModes (v, { 1.47f, 2.09f, 2.56f, 3.2f, 3.9f }, { 0.9f, 0.8f, 0.7f, 0.6f, 0.5f }, { 0.7f, 0.6f, 0.45f, 0.35f, 0.25f });
      withMetal (v, 0.2f, 0.6f, 4500, 400, 1200, 0.6f); withNoise (v, 0.1f, 400, 40, 2000); v.tailMs = 12000; k[China] = gain (v, 0.8f, 0.f); } // gong
    { auto v = body (1200, 0, 1, 2500, 0.5f); withModes (v, { 2.76f, 5.4f, 8.93f }, { 0.8f, 0.6f, 0.4f }, { 0.5f, 0.3f, 0.2f }); v.tailMs = 6000; k[Splash] = gain (v, 0.3f, 0.4f);
      auto m = v; m.bodyMs = 120; m.tailMs = 600; k[HatPedal] = gain (m, 0.2f, 0.4f); }
    { auto v = body (700, 0, 1, 45, 0.7f); withBody2 (v, 1650, 25, 0.4f); k[Rim] = gain (v, 0.5f, 0.2f); k[Perc] = gain (v, 0.5f, 0.2f); }  // wood block
    { auto v = noiseV (0.6f, 220, 7000, 16000); withMetal (v, 0.6f, 3.f, 240, 6000, 9500); v.bursts = 2; v.burstMs = 7; k[Tambourine] = gain (v, 0.42f, -0.3f); k[Shaker] = gain (v, 0.35f, -0.3f); }
    { auto v = body (1100, 1000, 3, 1500, 0.5f); withModes (v, { 2.2f, 3.4f }, { 0.6f, 0.4f }, { 0.4f, 0.2f }); v.tailMs = 4000; k[Cowbell] = gain (v, 0.4f, 0.1f); } // anvil-ish
    { auto v = clapV (4, 10.f, 1200, 1.f, 160); k[Clap] = gain (v, 0.5f); }
}
} // namespace

const VoiceSpec& synthSpecFor (int kitId, int lane, int voicing)
{
    struct Tables
    {
        KitTable t808, t909, tCR, tLinn, tCajon, tOrch;
        KitTable studio[NumStyles + 1]; // + brushes
        Tables()
        {
            build808 (t808); build909 (t909); buildCR78 (tCR); buildLinn (tLinn); buildCajon (tCajon); buildOrchestral (tOrch);
            for (int s = 0; s < NumStyles; ++s) buildStudio (studio[s], s);
            buildStudio (studio[NumStyles], 100);
        }
    };
    static const Tables T; // thread-safe one-time init
    const auto& t808 = T.t808; const auto& t909 = T.t909; const auto& tCR = T.tCR; const auto& tLinn = T.tLinn;
    const auto& tCajon = T.tCajon; const auto& tOrch = T.tOrch; const auto& studio = T.studio;
    static VoiceSpec silent;
    if (lane < 0 || lane >= NumLanes) return silent;
    switch (kitId)
    {
        case Kit808: return t808[lane];
        case Kit909: return t909[lane];
        case KitCR78: return tCR[lane];
        case KitLinn: return tLinn[lane];
        case KitCajonModel: return tCajon[lane];
        case KitOrchestralModel: return tOrch[lane];
        default:
        {
            const int v = voicing == 100 ? NumStyles : std::max (0, std::min (NumStyles - 1, voicing));
            return studio[v][lane];
        }
    }
}
} // namespace hammer
