#pragma once
// ============================================================================
//  FillSoloGenerator - procedural fills and solos with a per-style vocabulary
//  (tom runs, snare builds, linear funk fills, triplet blues/jazz fills,
//  blast fills, jazz comping, cajón slaps, timpani rolls, minimal pickups).
// ============================================================================
#include "StyleLibrary.h"

namespace hammer
{
struct FillParams
{
    int styleId = Rock;
    const StyleDef* style = nullptr;
    int ticksPerBar = 48;
    int startTick = 24;
    int endTick = 48;       // exclusive
    float intensity = 0.5f;
    float aggression = 0.4f;
    float improvise = 0.4f;
    float bpm = 120.f;
};

class FillSoloGenerator
{
public:
    static FillType chooseFill (const StyleDef& s, Rng& rng, float aggression);
    // Writes the fill into 'g' (prob 1 hits) for [startTick, endTick).
    static void fill (FillType type, const FillParams& p, LaneGrid& g, Rng& rng);
    // Which base lanes keep playing under this fill type (feet, jazz ride...)
    static bool keepsLane (FillType type, int lane);

    // A solo bar. barInSolo counts from 0; soloBars total length.
    static void solo (const FillParams& p, int barInSolo, int soloBars, LaneGrid& g, Rng& rng);
};
} // namespace hammer
