#pragma once
// ============================================================================
//  SynthVoices - every built-in drum is one flexible voice architecture:
//    body oscillator with pitch envelope  (808/909 kicks, toms)
//    2nd body / modal partials            (snares, membranes, timpani, cajón)
//    filtered noise                       (snare wires, hats, shakers, brushes)
//    6-oscillator metallic square bank    (808/909/CR-78 hats & cymbals, cowbell)
//    click / clap bursts / drive
//  Each kit = a table of VoiceSpecs, so new machines are just new numbers.
// ============================================================================
#include "../Common/HammerTypes.h"
#include "../Ears/OnsetAnalyzer.h"   // Biquad

namespace hammer
{
struct VoiceSpec
{
    bool valid = false;
    // body
    float bodyHz = 0, bodyEndHz = 0, pitchMs = 30, bodyMs = 200, bodyLvl = 0, bodyDrive = 0;
    float body2Hz = 0, body2Ms = 100, body2Lvl = 0;
    // modal partials (ratios relative to bodyHz)
    int modes = 0;
    float modeRatio[8] {}, modeDecay[8] {}, modeGain[8] {};
    // noise
    float noiseLvl = 0, noiseMs = 100, noiseAttMs = 0, noiseHp = 20, noiseLp = 18000, noiseBp = 0, noiseQ = 1;
    // metallic bank
    float metalLvl = 0, metalBase = 1.f, metalMs = 100, metalHp = 6000, metalBp = 8000, metalQ = 1.2f;
    int metalMask = 0x3F; // which of the 6 square oscillators sound (cowbell = 0x30: 540 + 800 Hz)
    // click
    float clickLvl = 0, clickMs = 2, clickHz = 3000;
    // clap
    int bursts = 0; float burstMs = 10;
    // behaviour
    float velDecay = 0.25f, velBright = 0.5f, velPitch = 0.f;
    float gain = 1.f, pan = 0.f;
    float tailMs = 1500;
    float outDrive = 0.f;
};

class SynthVoice
{
public:
    void start (const VoiceSpec& spec, float velocity, float tune, double sampleRate, uint32_t seed);
    // Adds into L/R. Returns false when finished.
    bool render (float* L, float* R, int n, float gainL, float gainR);
    void choke (double sampleRate, double ms = 12.0) { chokeRel = (float) std::exp (-1.0 / (sampleRate * ms * 0.001)); choking = true; }
    bool isActive() const { return active; }

private:
    VoiceSpec s;
    bool active = false, choking = false;
    float vel = 1, sr = 44100, t = 0, dt = 0;
    float phase = 0, phase2 = 0, modePhase[8] {}, metalPhase[6] {};
    float bodyCoef = 0, body2Coef = 0, noiseCoef = 0, metalCoef = 0, clickCoef = 0, pitchCoef = 0;
    float modeCoef[8] {}, modeEnv[8] {};
    float bodyEnv = 0, body2Env = 0, noiseEnv = 0, metalEnv = 0, clickEnv = 0, pitchEnv = 0, chokeEnv = 1, chokeRel = 0;
    float noiseAttCoef = 0, noiseAttEnv = 0;
    int samplesLeft = 0, sampleCount = 0;
    float tuneMul = 1;
    uint32_t rnd = 1;
    Biquad nHp, nLp, nBp, mBp, mHp;
    float clapEnv = 0; int burstIdx = 0; int nextBurst = 0; int burstSpacing = 0;
};

// Kit tables -------------------------------------------------------------
// voicing: style id, used by the Studio (modeled acoustic) kit to tune itself
const VoiceSpec& synthSpecFor (int kitId, int lane, int voicing);
} // namespace hammer
