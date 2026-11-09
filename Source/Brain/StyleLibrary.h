#pragma once
// ============================================================================
//  StyleLibrary - the drummer's "playbook": grooves at several intensities
//  per style, fill vocabulary, swing, humanise feel, ornaments, tempo comfort
//  and conditioning for the neural brain.
//
//  Pattern strings: one char per step.  16 steps = straight 16ths,
//  12 steps = 8th-note triplets (swing/shuffle), 24 = 16th triplets.
//    'X' accent  'x' normal  'o' medium  'f' soft  'g' ghost
//    '?' maybe (40%)   'h' maybe-ghost (50%)   '.' rest
// ============================================================================
#include "../Common/HammerTypes.h"

namespace hammer
{
enum KitId : int
{
    KitStudio = 0, Kit808, Kit909, KitCR78, KitLinn, KitCajonModel, KitOrchestralModel,
    KitSample1, KitSample2, KitSample3, KitSample4,
    NumKits
};
const char* kitName (int k);

enum FillType : int
{
    FillTomRun = 0, FillSnareBuild, FillLinear, FillTriplet, FillBlast, FillJazzComp,
    FillCajon, FillOrchestral, FillMinimal, FillKickSnare,
    NumFillTypes
};

constexpr int kNumGmdStyles = 18;
const char* gmdStyleName (int i);

struct LanePattern
{
    int lane = -1;
    const char* steps = nullptr;
};

struct GrooveDef
{
    const char* name = "";
    float intensity = 0.5f;   // where it sits on the intensity scale
    int steps = 16;           // resolution of the strings (per 4/4 bar)
    LanePattern lanes[9];
    bool halfTime = false;    // feels half-time (breakdowns)
};

struct HumanFeel
{
    float laneBiasMs[NumLanes] {}; // + = behind the beat (laid back)
    float jitterMs = 4.f;
    float velJitter = 0.06f;
    float accentHats = 0.25f;      // downbeat emphasis on hats/ride
};

struct Ornaments
{
    float ghostSnare = 0.f;   // ghost notes around backbeats
    float kickPickup = 0.f;   // 16th/triplet kick pickups into beats
    float openHatEnd = 0.f;   // open hat on last off-beat of a phrase
    float hatSubdiv = 0.f;    // tendency to go to 16ths with intensity
    float comping = 0.f;      // jazz-style snare/kick comping
    float bombs = 0.f;        // jazz kick "bombs"
    float rideBell = 0.f;     // bell accents
    float splash = 0.f;
    float doubleKick = 0.f;   // double-kick runs at high aggression
    float chinaWithAggression = 0.f;
};

struct StyleDef
{
    const char* name;
    bool tripletGrid;         // grooves are written on 12-step grid
    float swing8;             // 0..1 (1 = full triplet swing of 8ths)
    float swing16;            // 0..1 (1 = full 16th triplet swing)
    int defaultKit;           // built-in fallback kit
    const char* kitTag;       // preferred sample-kit tag (auto kit)
    GrooveDef grooves[5];
    int numGrooves;
    float fillVocab[NumFillTypes];
    HumanFeel feel;
    Ornaments orn;
    float minBpm, maxBpm;     // comfort zone for Auto feel
    float gmdMix[kNumGmdStyles];
    float crashAfterFill;     // base probability of a crash after a fill
    float baseVelocity;       // overall dynamic level
};

class StyleLibrary
{
public:
    static const StyleDef& get (int style);
    // Choose groove index for an intensity (0..1)
    static int grooveForIntensity (const StyleDef& s, float intensity);
    // Convert a pattern char to (probability, velocity)
    static bool decodeStep (char c, float& prob, float& vel);
};
} // namespace hammer
