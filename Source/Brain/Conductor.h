#pragma once
// ============================================================================
//  Conductor - the drummer's decision maker. Once per bar it:
//   1. works out the section / effective intensity (from your controls and
//      what the Ears hear), plans the phrase (fills, solo spots),
//   2. asks the LISTENING brain (playbook) and the NEURAL brain for what they
//      want to play and blends them with the BRAIN fader,
//   3. overlays fills / solos / crashes,
//   4. turns probabilities into hits, enforces two hands + two feet, and
//      humanises timing and velocity with the style's feel.
// ============================================================================
#include "PlaybookEngine.h"
#include "NeuralEngine.h"
#include "FillSoloGenerator.h"
#include <atomic>

namespace hammer
{
struct BarRequest
{
    int64_t barIndex = 0;
    int beatsPerBar = 4;
    float bpm = 120.f;
    const EngineSettings* settings = nullptr;
    const ListeningState* listening = nullptr;
    double nowPpq = 0.0;
    bool machineKit = false;     // drum machines: no limb limits, tighter timing
    bool ensembleKit = false;    // orchestral: several players, no limb limits
};

struct ConductorStatus
{
    std::atomic<int> style { 0 }, groove { 0 }, role { 0 }, section { 0 }, barInPhrase { 0 }, feel { 2 };
    std::atomic<float> intensity { 0.f }, energy { 0.f }, novelty { 0.f };
    std::atomic<bool> neuralModel { false }, listening { false };
    std::atomic<int64_t> bar { 0 };
};

class Conductor
{
public:
    Conductor();
    void setNeural (NeuralEngine* n) { neural = n; }
    void reset();

    void generateBar (const BarRequest& req, Bar& out);

    // Performance triggers (from MIDI keys or GUI)
    void triggerFill() { fillRequested = true; }
    void triggerCrash() { crashRequested = true; }
    void setSoloHeld (bool b) { soloHeld = b; }
    void setStopHeld (bool b) { stopHeld = b; }
    void setSectionOverride (int s) { sectionOverride = s; } // -1 = none
    void toggleHalfTime() { halfToggle = ! halfToggle; }

    // Fill-now inside an already generated bar from 'fromTick' to the end.
    bool injectFill (Bar& bar, int fromTick, const BarRequest& req);

    ConductorStatus status;

private:
    void planPhrase (const EngineSettings& s, int phraseBars, int beatsPerBar);
    void refreshRandomField (bool full, float improvise);
    void realise (const LaneGrid& g, Bar& out, int tpb);
    void enforceLimbs (Bar& bar, const BarRequest& req, const StyleDef& st, float aggression);
    void humanise (Bar& bar, const BarRequest& req, const StyleDef& st, float effIntensity);
    float sectionOffset (int section) const;
    int effectiveSection (const EngineSettings& s) const;

    PlaybookEngine playbook;
    NeuralEngine* neural = nullptr;
    Rng rng { 0xA9711 };
    Rng humanRng { 0x51CE };

    LaneGrid gp, gn, gb, special;
    float U[NumLanes][kMaxTicks];
    float thr[NumLanes][kMaxTicks];
    float acc[3][kMaxTicks];

    // phrase plan
    int phraseFillBeats = 0, midFillBeats = 0, turnFillBeats = 0;
    bool phraseSolo = false;
    int soloBars = 0;
    int soloBarCounter = 0;

    int prevRole = RoleGroove;
    int prevStyle = -1;
    int prevSection = -1;
    int prevTpb = 0;
    float smoothIntensity = -1.f;
    bool stopWasActive = false;

    bool fillRequested = false, crashRequested = false, soloHeld = false, stopHeld = false;
    bool halfToggle = false;
    int sectionOverride = -1;
    bool crashNextBar = false;

    Bar lockBar;
    bool haveLockBar = false;
};
} // namespace hammer
