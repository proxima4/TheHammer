#pragma once
// ============================================================================
//  ListeningState - musical memory of what The Hammer has heard.
//  Turns raw onsets (time-stamped in PPQ) into per-bar accent maps for the
//  three bands, tracks energy/density per bar, predicts the next bar's
//  accents from repetition (bars 1, 2, 4 and 8 back) and flags section
//  changes (novelty).
// ============================================================================
#include "../Common/HammerTypes.h"
#include <cstdint>

namespace hammer
{
struct OnsetRecord
{
    double ppq = 0.0;
    float band[3] {};
    float strength = 0.f;
};

struct ListeningFeatures
{
    float energy = 0.f;        // 0..1 normalised loudness of recent music
    float energyTrend = 0.f;   // -1..1 (falling .. rising)
    float density = 0.f;       // 0..1 onsets per beat (normalised)
    float novelty = 0.f;       // 0..1 how different the last bar was from the previous few
    float sixteenthFeel = 0.f; // 0..1 high-band activity on 16th off-beats
    float tripletFeel = 0.f;   // 0..1 activity on triplet positions
    float unison = 0.f;        // 0..1 how often all bands hit together (stabs)
    bool  hasData = false;
};

class ListeningState
{
public:
    ListeningState();
    void reset();
    void setMeter (int beatsPerBar);

    void addOnset (double ppq, const float band[3], float strength);
    void addEnergy (double ppq, float lowDb, float midDb, float highDb);

    // Predicted accent map for 'bar' (by repetition). out[band][tick], 0..1.
    void predictAccents (int64_t bar, float out[3][kMaxTicks]) const;
    // Accents actually heard so far in a bar
    void heardAccents (int64_t bar, float out[3][kMaxTicks]) const;

    ListeningFeatures features (double nowPpq) const;
    float barEnergy (int64_t bar) const;   // 0..1
    float noveltyOf (int64_t bar) const;   // 0..1
    bool hasRecentData (double nowPpq) const;

    int onsetsInRange (double ppqFrom, double ppqTo, OnsetRecord* out, int maxOut) const;

    int64_t barOf (double ppq) const { return (int64_t) std::floor (ppq / (double) beatsPerBar + 1e-9); }
    int ticksPerBar() const { return beatsPerBar * kTicksPerBeat; }

private:
    struct BarSlot
    {
        int64_t bar = -1000;
        float acc[3][kMaxTicks];
        double eSum[3] {};
        int eCount = 0;
        int onsets = 0;
        int highOff16 = 0, highTrip = 0, highTotal = 0, unison = 0;
        void clear (int64_t b);
    };

    BarSlot& slotFor (int64_t bar);
    const BarSlot* find (int64_t bar) const;

    static constexpr int kSlots = 16;
    BarSlot slots[kSlots];

    static constexpr int kRecent = 512;
    OnsetRecord recent[kRecent];
    int recentHead = 0, recentCount = 0;

    int beatsPerBar = 4;
    double lastOnsetPpq = -1e9, lastEnergyPpq = -1e9;
    float loudRefDb = -30.f;
};
} // namespace hammer
