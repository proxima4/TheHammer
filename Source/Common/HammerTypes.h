#pragma once
// ============================================================================
//  The Hammer - by APT Industrial
//  Core shared types: drum lanes, grid resolution, patterns, settings.
// ============================================================================
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace hammer
{
// ---------------------------------------------------------------------------
// Grid: 12 ticks per beat -> 16ths (every 3), 8th triplets (every 4),
// 16th triplets (every 2). Any meter up to 8 beats per bar.
// ---------------------------------------------------------------------------
constexpr int kTicksPerBeat = 12;
constexpr int kMaxBeatsPerBar = 8;
constexpr int kMaxTicks = kTicksPerBeat * kMaxBeatsPerBar; // 96
constexpr int kMaxHitsPerBar = 384;

// ---------------------------------------------------------------------------
// Drum lanes - universal "limbs" vocabulary. Each kit maps these to sounds
// (e.g. on the cajón kit Kick = bass tone, Snare = slap, Rim = tip).
// ---------------------------------------------------------------------------
enum Lane : int
{
    Kick = 0, Snare, Rim, HatClosed, HatOpen, HatPedal,
    TomHigh, TomMid, TomFloor,
    Crash, Crash2, Ride, RideBell, China, Splash,
    Clap, Cowbell, Shaker, Tambourine, Perc,
    NumLanes
};

const char* laneName (int lane);
const char* laneShortName (int lane);
int laneToGmNote (int lane);
int gmNoteToLane (int note); // -1 if unmapped

// Output / mixer groups
enum Group : int { GKick = 0, GSnare, GToms, GHats, GCymbals, GPerc, NumGroups };
int laneGroup (int lane);
const char* groupName (int g);

enum class Limb : uint8_t { RightHand, LeftHand, RightFoot, LeftFoot, Either };
bool laneIsHand (int lane);
bool laneIsFoot (int lane);

// ---------------------------------------------------------------------------
// Styles
// ---------------------------------------------------------------------------
enum StyleId : int
{
    Jazz = 0, Rock, Metalcore, DeathMetal, Indie, FusionJazz,
    Funk, Groove, Blues, Orchestral, Minimal, Cajon,
    NumStyles
};
const char* styleName (int s);

enum class Feel : int { Auto = 0, Half, Normal, Double };
enum class Section : int { Auto = 0, Intro, Verse, PreChorus, Chorus, Bridge, Breakdown, Solo, Outro, Stop, NumSections };
const char* sectionName (int s);

// ---------------------------------------------------------------------------
// Pattern data
// ---------------------------------------------------------------------------
struct Hit
{
    uint8_t lane = 0;
    uint8_t tick = 0;      // 0 .. ticksPerBar-1
    float velocity = 0.8f; // 0..1
    float offsetMs = 0.f;  // humanised micro-timing (positive = late)
    float offsetTicks = 0.f; // swing / neural micro-timing in ticks
};

struct Bar
{
    std::array<Hit, kMaxHitsPerBar> hits {};
    int numHits = 0;
    int ticksPerBar = 48;
    int beatsPerBar = 4;
    int role = 0; // BarRole

    void clear() { numHits = 0; }
    bool add (const Hit& h)
    {
        if (numHits >= kMaxHitsPerBar) return false;
        hits[(size_t) numHits++] = h;
        return true;
    }
    bool has (int lane, int tick) const
    {
        for (int i = 0; i < numHits; ++i)
            if (hits[(size_t) i].lane == lane && hits[(size_t) i].tick == tick) return true;
        return false;
    }
    void removeIf (int lane, int tick)
    {
        for (int i = 0; i < numHits; ++i)
            if (hits[(size_t) i].lane == lane && hits[(size_t) i].tick == tick)
            {
                hits[(size_t) i] = hits[(size_t) (numHits - 1)];
                --numHits; --i;
            }
    }
};

enum BarRole : int { RoleGroove = 0, RoleFill = 1, RoleSolo = 2, RoleStop = 3, RoleSectionStart = 4 };

// Probability field: what a drummer "wants" to play in a bar.
struct LaneGrid
{
    float prob[NumLanes][kMaxTicks];
    float vel[NumLanes][kMaxTicks];
    float offs[NumLanes][kMaxTicks]; // micro timing in ticks
    int ticksPerBar = 48;

    void clear (int tpb)
    {
        ticksPerBar = tpb;
        for (int l = 0; l < NumLanes; ++l)
            for (int t = 0; t < kMaxTicks; ++t) { prob[l][t] = 0.f; vel[l][t] = 0.f; offs[l][t] = 0.f; }
    }
    void set (int lane, int tick, float p, float v)
    {
        if (tick < 0 || tick >= ticksPerBar) return;
        prob[lane][tick] = std::max (prob[lane][tick], p);
        vel[lane][tick] = std::max (vel[lane][tick], v);
    }
};

// ---------------------------------------------------------------------------
// Settings snapshot handed to the engine each block (filled from params).
// ---------------------------------------------------------------------------
struct EngineSettings
{
    int style = Rock;
    float intensity = 0.55f;   // 0..1
    float aggression = 0.4f;   // 0..1
    float fills = 0.4f;        // 0..1
    float solos = 0.1f;        // 0..1
    float follow = 0.5f;       // 0 = subtle, 1 = follow exactly
    float improvise = 0.4f;    // 0 = repeat, 1 = wild
    float brain = 0.35f;       // 0 = listening/playbook, 1 = neural
    float swing = 1.0f;        // multiplier of style swing (0..2)
    float humanize = 0.6f;     // 0..1
    float dynamics = 0.6f;     // velocity range
    Feel feel = Feel::Auto;
    Section section = Section::Auto;
    int phraseBars = 8;
    bool drummerOn = true;
    bool midiOut = false;
    bool lockGroove = false;   // freeze: keep replaying the current groove
    float lookaheadMs = 0.f;
    float earsSensitivity = 0.5f;
    float internalBpm = 120.f;
};

inline float clamp01 (float x) { return std::min (1.f, std::max (0.f, x)); }
inline float lerp (float a, float b, float t) { return a + (b - a) * t; }

// Tiny fast deterministic RNG (xorshift) - real-time safe.
struct Rng
{
    uint64_t s = 0x9E3779B97F4A7C15ull;
    explicit Rng (uint64_t seed = 1234567) { setSeed (seed); }
    void setSeed (uint64_t seed) { s = seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull; if (s == 0) s = 1; }
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    float uni() { return (float) ((next() >> 40) * (1.0 / 16777216.0)); }       // [0,1)
    float range (float a, float b) { return a + (b - a) * uni(); }
    int below (int n) { return n <= 0 ? 0 : (int) (next() % (uint64_t) n); }
    bool chance (float p) { return uni() < p; }
    float gauss() // approx N(0,1)
    {
        float a = uni() + uni() + uni() + uni() + uni() + uni();
        return (a - 3.f) * 1.41421356f;
    }
};

} // namespace hammer
