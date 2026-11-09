#pragma once
// ============================================================================
//  HammerEngine - one complete drummer: ears + brain + kit + timing.
//  Used by the Drummer instrument, the mixer-insert FX and the offline tool.
// ============================================================================
#include "../Common/HammerTypes.h"
#include "../Ears/OnsetAnalyzer.h"
#include "../Ears/ListeningState.h"
#include "../Ears/SharedLink.h"
#include "../Brain/Conductor.h"
#include "../Sound/DrumKitEngine.h"
#include "MidiRecorder.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace hammer
{
struct TransportInfo
{
    bool playing = false;
    bool valid = false;
    double ppq = 0.0;
    double bpm = 120.0;
    int numerator = 4, denominator = 4;
};

struct DisplaySnapshot
{
    int64_t bar = -1;
    int ticksPerBar = 48;
    int numHits = 0;
    Hit hits[kMaxHitsPerBar];
    int role = 0;
};

class HammerEngine
{
public:
    enum class Mode { Drummer, Mixer, Tool };
    explicit HammerEngine (Mode m);

    void prepare (double sampleRate, int maxBlock);
    void reset();

    struct IO
    {
        const float* analysisL = nullptr; // audio to listen to (mixer mode / tool)
        const float* analysisR = nullptr;
        float* outL = nullptr;            // drums are ADDED here
        float* outR = nullptr;
        float* const* auxL = nullptr;     // optional multi-out (NumGroups)
        float* const* auxR = nullptr;
        juce::MidiBuffer* midiIn = nullptr;
        juce::MidiBuffer* midiOut = nullptr;
        int numSamples = 0;
    };
    void process (const TransportInfo& t, const EngineSettings& s, const SoundSettings& snd,
                  int kitParam, bool autoKit, int linkChannel, IO& io);

    // Internal transport (standalone / tool)
    void setInternalPlay (bool p) { internalPlaying = p; if (! p) internalPpq = 0.0; }
    bool isInternalPlaying() const { return internalPlaying; }

    // GUI helpers
    void copyDisplay (DisplaySnapshot& out) const;
    float playheadTick() const { return playTick.load(); }
    bool linkConnected() const { return linkAlive.load(); }
    void earsMeters (float out[3]) const { for (int i = 0; i < 3; ++i) out[i] = meters[i].load(); }
    int effectiveKit() const { return effKit.load(); }
    juce::String kitDisplayName() const;
    void requestFill() { fillNow = true; }
    // Silence everything (project load / new state) - handled on the audio thread
    void requestSilence() { silenceNow = true; }
    void requestCrash() { conductor.triggerCrash(); }
    void setSoloHeld (bool b) { conductor.setSoloHeld (b); }
    void setStopHeld (bool b) { conductor.setStopHeld (b); }
    bool exportMidi (const juce::File& f, int bars) const;
    int latencySamples (const EngineSettings& s) const;

    Conductor conductor;
    NeuralEngine neural;
    KitSlots kits;
    DrumKitEngine drums;
    ListeningState listening;
    OnsetAnalyzer analyzer;
    SharedLink link;
    MidiRecorder recorder;
    Mode mode;

private:
    struct BarSlot { int64_t index = INT64_MIN; Bar bar; };
    BarSlot* slotFor (int64_t idx);
    BarSlot* ensureBar (int64_t idx, const TransportInfo& t, const EngineSettings& s, double nowPpq);
    void handleMidiIn (juce::MidiBuffer& in, const EngineSettings& s, int n);
    void listenToAudio (const float* L, const float* R, int n, double ppqStartHost, double ppqPerSample, bool playing);
    void pollLink (int channel);
    void reactiveFollow (const EngineSettings& s, double renderFrom, double renderTo, int beatsPerBar);
    void schedule (BarSlot& b, double p0, double p1, double ppqPerSample, int bpb, const EngineSettings& s, IO& io);
    void publishDisplay (const BarSlot& b);
    void chooseKit (const EngineSettings& s, int kitParam, bool autoKit);

    double sr = 44100.0;
    int maxBlock = 512;
    BarSlot bars[3];
    double expectedPpq = -1e9;
    bool wasPlaying = false;
    bool internalPlaying = false;
    double internalPpq = 0.0;
    std::atomic<bool> fillNow { false };
    std::atomic<bool> silenceNow { false };
    int lastBpb = 4;
    int lastStyle = -1;

    // mixer-mode lookahead delay line for the pass-through audio
    juce::AudioBuffer<float> delayLine;
    int delayWrite = 0;

    // pending MIDI note-offs
    struct NoteOff { int note; int samplesLeft; };
    NoteOff noteOffs[128];
    int numNoteOffs = 0;

    OnsetAnalyzer::Event events[OnsetAnalyzer::kMaxEventsPerBlock];
    LinkEvent linkEvents[512];
    double lastReactivePpq = -1e9;

    mutable juce::SpinLock displayLock;
    DisplaySnapshot display;
    std::atomic<float> playTick { 0.f };
    std::atomic<bool> linkAlive { false };
    std::atomic<float> meters[3];
    std::atomic<int> effKit { KitStudio };
    double lastBpm = 120.0;
    int voicing = Rock;
    float lastEffIntensity = 0.5f;

public:
    // The sample-rate dependent pass-through delay helper (mixer mode)
    void processPassThrough (const float* inL, const float* inR, float* outL, float* outR, int n, int delaySamples);
};
} // namespace hammer
