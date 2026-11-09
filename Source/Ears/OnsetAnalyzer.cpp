#include "OnsetAnalyzer.h"
#include <algorithm>

namespace hammer
{
static constexpr double kPi = 3.14159265358979323846;

void Biquad::setLowpass (double s, double f, double q)
{
    double w = 2 * kPi * f / s, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
    b0 = (float) ((1 - c) / 2 / a0); b1 = (float) ((1 - c) / a0); b2 = b0;
    a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
}
void Biquad::setHighpass (double s, double f, double q)
{
    double w = 2 * kPi * f / s, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
    b0 = (float) ((1 + c) / 2 / a0); b1 = (float) (-(1 + c) / a0); b2 = b0;
    a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
}
void Biquad::setBandpass (double s, double f, double q)
{
    double w = 2 * kPi * f / s, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
    b0 = (float) (al / a0); b1 = 0; b2 = (float) (-al / a0);
    a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
}
void Biquad::setPeak (double s, double f, double q, double g)
{
    double A = std::pow (10.0, g / 40.0), w = 2 * kPi * f / s, c = std::cos (w), al = std::sin (w) / (2 * q);
    double a0 = 1 + al / A;
    b0 = (float) ((1 + al * A) / a0); b1 = (float) (-2 * c / a0); b2 = (float) ((1 - al * A) / a0);
    a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al / A) / a0);
}
void Biquad::setLowShelf (double s, double f, double g)
{
    double A = std::pow (10.0, g / 40.0), w = 2 * kPi * f / s, c = std::cos (w), sn = std::sin (w);
    double al = sn / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
    double a0 = (A + 1) + (A - 1) * c + sq;
    b0 = (float) (A * ((A + 1) - (A - 1) * c + sq) / a0);
    b1 = (float) (2 * A * ((A - 1) - (A + 1) * c) / a0);
    b2 = (float) (A * ((A + 1) - (A - 1) * c - sq) / a0);
    a1 = (float) (-2 * ((A - 1) + (A + 1) * c) / a0);
    a2 = (float) (((A + 1) + (A - 1) * c - sq) / a0);
}
void Biquad::setHighShelf (double s, double f, double g)
{
    double A = std::pow (10.0, g / 40.0), w = 2 * kPi * f / s, c = std::cos (w), sn = std::sin (w);
    double al = sn / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
    double a0 = (A + 1) - (A - 1) * c + sq;
    b0 = (float) (A * ((A + 1) + (A - 1) * c + sq) / a0);
    b1 = (float) (-2 * A * ((A - 1) + (A + 1) * c) / a0);
    b2 = (float) (A * ((A + 1) + (A - 1) * c - sq) / a0);
    a1 = (float) (2 * ((A - 1) - (A + 1) * c) / a0);
    a2 = (float) (((A + 1) - (A - 1) * c - sq) / a0);
}

static float coefFor (double hopRate, double ms)
{
    return (float) std::exp (-1.0 / (hopRate * ms * 0.001));
}

void OnsetAnalyzer::prepare (double sampleRate)
{
    sr = sampleRate;
    lp1.setLowpass (sr, 150.0); lp2.setLowpass (sr, 150.0);
    hp1.setHighpass (sr, 4500.0); hp2.setHighpass (sr, 4500.0);
    mhp.setHighpass (sr, 180.0); mlp.setLowpass (sr, 2800.0);
    hhp1.setHighpass (sr, 40.0); hhp2.setHighpass (sr, 40.0);
    const double hopRate = sr / kHop;
    fastAtt = coefFor (hopRate, 0.5);
    fastRel = coefFor (hopRate, 18.0);
    slowAtt = coefFor (hopRate, 40.0);
    slowRel = coefFor (hopRate, 180.0);
    peakDecay = coefFor (hopRate, 6000.0);
    meterRel = coefFor (hopRate, 120.0);
    reset();
}

void OnsetAnalyzer::reset()
{
    for (auto* b : { &lp1, &lp2, &hp1, &hp2, &mhp, &mlp, &hhp1, &hhp2 }) b->reset();
    hopCount = 0;
    for (int i = 0; i < 3; ++i)
    {
        acc[i] = fastEnv[i] = slowEnv[i] = meter[i] = prevTransient[i] = 0.f;
        peakRef[i] = 1e-6f; slowDb[i] = -90.f; refractory[i] = 0; armed[i] = true;
        pendingBand[i] = 0.f;
    }
    pending = false;
    sampleClock = 0;
}

int OnsetAnalyzer::process (const float* L, const float* R, int n, Event* out, int maxOut)
{
    int count = 0;
    const double hopMs = 1000.0 * kHop / sr;
    // Threshold: 4 dB (very sensitive) .. 11 dB (only big hits)
    const float threshDb = 11.f - 7.f * sensitivity;
    const int refractoryHops[3] = { (int) (70.0 / hopMs), (int) (55.0 / hopMs), (int) (45.0 / hopMs) };
    const int mergeHops = std::max (1, (int) (9.0 / hopMs));

    for (int i = 0; i < n; ++i)
    {
        float x = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];
        x = hhp2.process (hhp1.process (x)); // remove DC / rumble
        const float lo = lp2.process (lp1.process (x));
        const float hi = hp2.process (hp1.process (x));
        const float mi = mlp.process (mhp.process (x));
        acc[0] += lo * lo; acc[1] += mi * mi; acc[2] += hi * hi;

        if (++hopCount < kHop) continue;
        hopCount = 0;

        float trans[3];
        for (int b = 0; b < 3; ++b)
        {
            const float ms = acc[b] / (float) kHop + 1e-12f;
            acc[b] = 0.f;
            const float lvl = std::sqrt (ms);
            fastEnv[b] = lvl > fastEnv[b] ? fastAtt * fastEnv[b] + (1 - fastAtt) * lvl : fastRel * fastEnv[b] + (1 - fastRel) * lvl;
            slowEnv[b] = lvl > slowEnv[b] ? slowAtt * slowEnv[b] + (1 - slowAtt) * lvl : slowRel * slowEnv[b] + (1 - slowRel) * lvl;
            peakRef[b] = std::max (fastEnv[b], peakRef[b] * peakDecay);
            peakRef[b] = std::max (peakRef[b], 1e-5f);

            const float fastDb = 20.f * std::log10 (fastEnv[b] + 1e-9f);
            const float sDb = 20.f * std::log10 (slowEnv[b] + 1e-9f);
            slowDb[b] = sDb;
            trans[b] = fastDb - sDb;

            const float rel = fastEnv[b] / peakRef[b]; // 0..1
            const float m = std::min (1.f, rel);
            meter[b] = m > meter[b] ? m : meterRel * meter[b] + (1 - meterRel) * m;

            if (refractory[b] > 0) --refractory[b];
            if (! armed[b] && trans[b] < threshDb * 0.4f) armed[b] = true;

            const bool loudEnough = fastDb > -62.f && rel > 0.08f;
            if (armed[b] && refractory[b] == 0 && loudEnough && trans[b] > threshDb)
            {
                // strength: how far above threshold + how loud vs recent peak
                const float over = std::min (1.f, (trans[b] - threshDb) / 12.f + 0.35f);
                const float loud = std::min (1.f, std::sqrt (rel));
                const float s = std::min (1.f, over * 0.45f + loud * 0.65f);
                armed[b] = false;
                refractory[b] = refractoryHops[b];
                if (! pending)
                {
                    pending = true;
                    pendingHopsLeft = mergeHops;
                    pendingSample = i;
                    for (int k = 0; k < 3; ++k) pendingBand[k] = 0.f;
                }
                pendingBand[b] = std::max (pendingBand[b], s);
            }
            prevTransient[b] = trans[b];
        }

        if (pending && --pendingHopsLeft <= 0)
        {
            pending = false;
            if (count < maxOut)
            {
                Event& e = out[count++];
                e.sampleOffset = std::max (0, std::min (n - 1, pendingSample));
                for (int k = 0; k < 3; ++k) e.band[k] = pendingBand[k];
                e.strength = std::min (1.f, 0.5f * pendingBand[0] + 0.35f * pendingBand[1] + 0.25f * pendingBand[2]
                                             + 0.25f * std::max ({ pendingBand[0], pendingBand[1], pendingBand[2] }));
            }
        }
    }
    // A pending event spilling into the next block keeps its offset at 0
    if (pending) pendingSample = 0;
    sampleClock += n;
    return count;
}
} // namespace hammer
