#pragma once
// ============================================================================
//  DrumKitEngine - plays hits on the chosen kit (modeled machines/acoustic or
//  sample kits), with hi-hat choke, per-group mixer, optional multi-outs,
//  room reverb send, "Hammer" drive and glue compression.
// ============================================================================
#include "SynthVoices.h"
#include "SampleKit.h"
#include "../Brain/StyleLibrary.h"
#include <juce_dsp/juce_dsp.h>

namespace hammer
{
struct SoundSettings
{
    float groupGain[NumGroups] { 1, 1, 1, 1, 1, 1 };
    float master = 0.8f;     // linear
    float room = 0.2f;       // 0..1 send
    float roomSize = 0.5f;   // 0..1
    float drive = 0.15f;     // 0..1
    float glue = 0.3f;       // 0..1
    float tuneSemis = 0.f;
    float velCurve = 0.5f;   // 0 = flat .. 1 = very dynamic
};

class DrumKitEngine
{
public:
    static constexpr int kVoices = 96;

    void prepare (double sampleRate, int maxBlock);
    void setKit (int kitId, int voicing, SampleKitData* sampleKit);
    void trigger (int lane, float velocity, int sampleOffset);
    void allNotesOff();
    // Transport stop / project load: fade every voice AND the room tail to silence (~15 ms, click-free)
    void fadeOutAll();

    // main: stereo buffer to ADD into (cleared by caller). groupOuts may be null.
    void render (float* mainL, float* mainR, int numSamples, float* const* groupOutsL, float* const* groupOutsR);

    SoundSettings settings;
    int activeVoices() const { return numActive; }
    float laneMeter (int lane) const { return laneFlash[lane]; }
    void decayMeters (float k) { for (auto& f : laneFlash) f *= k; }

private:
    struct Voice
    {
        bool active = false;
        int lane = 0, group = 0, delay = 0;
        bool isSample = false;
        SynthVoice synth;
        const juce::AudioBuffer<float>* buf = nullptr;
        double pos = 0, rate = 1;
        float gain = 1, gl = 1, gr = 1;
        bool choking = false; float chokeEnv = 1, chokeK = 0.999f;
        uint64_t age = 0;
    };
    Voice* allocVoice (int lane);
    void renderVoice (Voice& v, float* L, float* R, int n);

    Voice voices[kVoices];
    double sr = 44100.0;
    int kit = KitStudio, voicingId = Rock;
    SampleKitData* sample = nullptr;
    uint64_t ageCounter = 0;
    uint32_t seed = 1;
    int rrLast[NumLanes] {};
    int numActive = 0;
    float laneFlash[NumLanes] {};
    Rng rng { 777 };

    juce::AudioBuffer<float> groupBuf, sendBuf;
    juce::Reverb reverb;
    float compEnv = 0.f;
    Biquad driveTilt[2], roomHp[2];
    float lastRoomSize = -1.f;
    int roomFadeLeft = 0, roomFadeLen = 1;
};
} // namespace hammer
