#pragma once
// ============================================================================
//  OnsetAnalyzer - real-time, allocation-free listener.
//  Splits audio into LOW (kick/bass) MID (snare/guitars/keys) HIGH (hats,
//  cymbals, pick attack) and detects accents in each band using a fast/slow
//  envelope transient detector with adaptive thresholds.
// ============================================================================
#include <cmath>
#include <array>

namespace hammer
{
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process (float x)
    {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void setLowpass (double sr, double f, double q = 0.7071);
    void setHighpass (double sr, double f, double q = 0.7071);
    void setBandpass (double sr, double f, double q);
    void setPeak (double sr, double f, double q, double gainDb);
    void setLowShelf (double sr, double f, double gainDb);
    void setHighShelf (double sr, double f, double gainDb);
};

class OnsetAnalyzer
{
public:
    struct Event
    {
        int sampleOffset = 0;   // within the processed block
        float band[3] {};       // low, mid, high strengths 0..1
        float strength = 0.f;   // overall 0..1
    };

    static constexpr int kMaxEventsPerBlock = 64;

    void prepare (double sampleRate);
    void reset();
    void setSensitivity (float s01) { sensitivity = s01; }

    // Analyse a block (stereo or mono - pass R = nullptr for mono).
    // Returns number of events written to 'out'.
    int process (const float* L, const float* R, int numSamples, Event* out, int maxOut);

    // Smoothed band levels (0..1, relative to recent loudest) for meters / energy.
    float bandLevel (int b) const { return meter[b]; }
    // Slow band energy in dBFS (for section/energy tracking)
    float bandEnergyDb (int b) const { return slowDb[b]; }
    float overallLevel() const { return (meter[0] + meter[1] + meter[2]) / 3.f; }

private:
    double sr = 44100.0;
    float sensitivity = 0.5f;

    Biquad lp1, lp2, hp1, hp2, mhp, mlp, hhp1, hhp2;

    static constexpr int kHop = 64;
    int hopCount = 0;
    float acc[3] {};

    float fastEnv[3] {}, slowEnv[3] {}, peakRef[3] {};
    float slowDb[3] { -90.f, -90.f, -90.f };
    float meter[3] {};
    float prevTransient[3] {};
    int refractory[3] {};      // in hops
    bool armed[3] { true, true, true };

    // pending merged event
    bool pending = false;
    int pendingHopsLeft = 0;
    int pendingSample = 0;
    float pendingBand[3] {};

    float fastAtt = 0, fastRel = 0, slowAtt = 0, slowRel = 0, peakDecay = 0, meterRel = 0;
    long long sampleClock = 0;
};
} // namespace hammer
