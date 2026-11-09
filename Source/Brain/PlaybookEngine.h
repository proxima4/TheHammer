#pragma once
// ============================================================================
//  PlaybookEngine - the LISTENING side of the brain.
//  Picks a groove from the style playbook for the current intensity, then
//  bends it toward what the Ears heard: kicks lock to low-end accents,
//  snare/crash catch band stabs, hats follow the high-band subdivision.
//  FOLLOW (subtle .. exact) sets how strongly it locks.
// ============================================================================
#include "StyleLibrary.h"
#include "../Ears/ListeningState.h"

namespace hammer
{
struct BrainContext
{
    int styleId = Rock;
    const StyleDef* style = nullptr;
    int ticksPerBar = 48;
    int beatsPerBar = 4;
    int64_t barIndex = 0;
    int barInPhrase = 0;
    int phraseBars = 8;
    float intensity = 0.5f;
    float aggression = 0.4f;
    float follow = 0.5f;
    float improvise = 0.4f;
    float bpm = 120.f;
    bool halfTime = false;
    bool doubleTime = false;
    bool phraseStart = false;
    bool sectionChange = false;
    bool forceHalfTimeGroove = false;   // breakdown
    const float (*accents)[kMaxTicks] = nullptr; // [3][kMaxTicks] predicted
    ListeningFeatures lf;
    // neural specific
    int fillFromTick = -1;              // >=0 -> fill flag from this tick on
};

class PlaybookEngine
{
public:
    void reset();
    void generate (const BrainContext& ctx, LaneGrid& out, Rng& rng);
    const char* currentGrooveName() const { return grooveName; }
    int currentGrooveIndex() const { return grooveIdx; }

    // Write a groove definition into a grid (handles meter, half/double time)
    static void renderGroove (const GrooveDef& g, int ticksPerBar, int beatsPerBar,
                              bool halfTime, bool doubleTime, int64_t barIndex, LaneGrid& out);

private:
    void applyListening (const BrainContext& ctx, LaneGrid& g);
    void applyOrnaments (const BrainContext& ctx, LaneGrid& g, Rng& rng);
    void applyAggression (const BrainContext& ctx, LaneGrid& g, Rng& rng);

    int grooveIdx = -1;
    int styleOfGroove = -1;
    const char* grooveName = "";
    int chinaPhrase = -1; // china-instead-of-crash decided per phrase
};
} // namespace hammer
