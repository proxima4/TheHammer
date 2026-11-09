#include "ListeningState.h"
#include <cmath>
#include <algorithm>

namespace hammer
{
void ListeningState::BarSlot::clear (int64_t b)
{
    bar = b;
    for (int k = 0; k < 3; ++k)
    {
        eSum[k] = 0.0;
        for (int t = 0; t < kMaxTicks; ++t) acc[k][t] = 0.f;
    }
    eCount = onsets = highOff16 = highTrip = highTotal = unison = 0;
}

ListeningState::ListeningState() { reset(); }

void ListeningState::reset()
{
    for (auto& s : slots) s.clear (-1000);
    recentHead = recentCount = 0;
    lastOnsetPpq = lastEnergyPpq = -1e9;
    loudRefDb = -30.f;
}

void ListeningState::setMeter (int bpb)
{
    bpb = std::max (1, std::min (kMaxBeatsPerBar, bpb));
    if (bpb != beatsPerBar)
    {
        beatsPerBar = bpb;
        for (auto& s : slots) s.clear (-1000);
    }
}

ListeningState::BarSlot& ListeningState::slotFor (int64_t bar)
{
    auto& s = slots[(size_t) (((bar % kSlots) + kSlots) % kSlots)];
    if (s.bar != bar) s.clear (bar);
    return s;
}

const ListeningState::BarSlot* ListeningState::find (int64_t bar) const
{
    const auto& s = slots[(size_t) (((bar % kSlots) + kSlots) % kSlots)];
    return s.bar == bar ? &s : nullptr;
}

void ListeningState::addOnset (double ppq, const float band[3], float strength)
{
    const int64_t bar = barOf (ppq);
    const double inBar = ppq - (double) bar * beatsPerBar;
    int tick = (int) std::lround (inBar * kTicksPerBeat);
    int64_t useBar = bar;
    if (tick >= ticksPerBar()) { tick = 0; ++useBar; }
    auto& s = slotFor (useBar);
    for (int k = 0; k < 3; ++k)
        s.acc[k][tick] = std::max (s.acc[k][tick], band[k]);
    s.onsets++;
    if (band[2] > 0.2f)
    {
        s.highTotal++;
        const int inBeat = tick % kTicksPerBeat;
        if (inBeat == 3 || inBeat == 9) s.highOff16++;
        if (inBeat == 4 || inBeat == 8) s.highTrip++;
    }
    if (band[0] > 0.35f && band[1] > 0.35f && band[2] > 0.3f) s.unison++;

    auto& r = recent[(size_t) recentHead];
    r.ppq = ppq; r.strength = strength;
    for (int k = 0; k < 3; ++k) r.band[k] = band[k];
    recentHead = (recentHead + 1) % kRecent;
    recentCount = std::min (recentCount + 1, kRecent);
    lastOnsetPpq = ppq;
}

void ListeningState::addEnergy (double ppq, float lowDb, float midDb, float highDb)
{
    auto& s = slotFor (barOf (ppq));
    const double lin[3] = { std::pow (10.0, lowDb / 20.0), std::pow (10.0, midDb / 20.0), std::pow (10.0, highDb / 20.0) };
    for (int k = 0; k < 3; ++k) s.eSum[k] += lin[k];
    s.eCount++;
    const float overall = 20.f * (float) std::log10 (lin[0] * 0.45 + lin[1] * 0.4 + lin[2] * 0.15 + 1e-9);
    // loud reference: rises fast, decays slowly (about 1 dB / 4 bars of silence)
    if (overall > loudRefDb) loudRefDb = overall;
    else loudRefDb -= 0.0005f;
    loudRefDb = std::max (loudRefDb, -60.f);
    lastEnergyPpq = ppq;
}

float ListeningState::barEnergy (int64_t bar) const
{
    const auto* s = find (bar);
    if (s == nullptr || s->eCount == 0) return 0.f;
    const double l = s->eSum[0] / s->eCount, m = s->eSum[1] / s->eCount, h = s->eSum[2] / s->eCount;
    const float db = 20.f * (float) std::log10 (l * 0.45 + m * 0.4 + h * 0.15 + 1e-9);
    return clamp01 ((db - (loudRefDb - 24.f)) / 24.f);
}

float ListeningState::noveltyOf (int64_t bar) const
{
    const auto* s = find (bar);
    if (s == nullptr) return 0.f;
    const float e = barEnergy (bar);
    const float d = std::min (1.f, (float) s->onsets / (float) (beatsPerBar * 4));
    float meanE = 0.f, meanD = 0.f; int n = 0;
    for (int64_t b = bar - 4; b < bar; ++b)
        if (const auto* p = find (b))
        {
            meanE += barEnergy (b);
            meanD += std::min (1.f, (float) p->onsets / (float) (beatsPerBar * 4));
            ++n;
        }
    if (n == 0) return 0.f;
    meanE /= (float) n; meanD /= (float) n;
    return clamp01 (std::abs (e - meanE) * 2.2f + std::abs (d - meanD) * 1.2f);
}

void ListeningState::heardAccents (int64_t bar, float out[3][kMaxTicks]) const
{
    for (int k = 0; k < 3; ++k) for (int t = 0; t < kMaxTicks; ++t) out[k][t] = 0.f;
    if (const auto* s = find (bar))
        for (int k = 0; k < 3; ++k) for (int t = 0; t < kMaxTicks; ++t) out[k][t] = s->acc[k][t];
}

void ListeningState::predictAccents (int64_t bar, float out[3][kMaxTicks]) const
{
    for (int k = 0; k < 3; ++k) for (int t = 0; t < kMaxTicks; ++t) out[k][t] = 0.f;
    // Music repeats: look 1, 2, 4 and 8 bars back. If bar-4 and bar-8 agree
    // strongly with each other, we're probably in a 4-bar loop -> trust them more.
    const int64_t back[4] = { 1, 2, 4, 8 };
    float w[4] = { 0.42f, 0.18f, 0.28f, 0.12f };
    const BarSlot* src[4];
    for (int i = 0; i < 4; ++i) src[i] = find (bar - back[i]);

    if (src[2] && src[3])
    {
        float dot = 0, n1 = 0, n2 = 0;
        for (int t = 0; t < ticksPerBar(); ++t)
            for (int k = 0; k < 3; ++k)
            {
                dot += src[2]->acc[k][t] * src[3]->acc[k][t];
                n1 += src[2]->acc[k][t] * src[2]->acc[k][t];
                n2 += src[3]->acc[k][t] * src[3]->acc[k][t];
            }
        const float sim = (n1 > 0 && n2 > 0) ? dot / std::sqrt (n1 * n2) : 0.f;
        if (sim > 0.6f) { w[2] += 0.2f * sim; w[3] += 0.1f * sim; w[0] -= 0.1f; }
    }
    float wsum = 0.f;
    for (int i = 0; i < 4; ++i)
        if (src[i]) wsum += w[i];
    if (wsum <= 0.f) return;
    for (int i = 0; i < 4; ++i)
    {
        if (! src[i]) continue;
        const float ww = w[i] / wsum;
        for (int k = 0; k < 3; ++k)
            for (int t = 0; t < ticksPerBar(); ++t)
                out[k][t] += src[i]->acc[k][t] * ww;
    }
    // Sharpen a little so consistent accents stand out over one-off noise.
    for (int k = 0; k < 3; ++k)
        for (int t = 0; t < ticksPerBar(); ++t)
            out[k][t] = clamp01 (out[k][t] * 1.15f);
}

ListeningFeatures ListeningState::features (double nowPpq) const
{
    ListeningFeatures f;
    f.hasData = hasRecentData (nowPpq);
    if (! f.hasData) return f;
    const int64_t cur = barOf (nowPpq);
    // energy of the most recent complete bar blended with the current partial bar
    const float ePrev = barEnergy (cur - 1), eCur = barEnergy (cur);
    f.energy = std::max (ePrev, eCur * 0.9f);
    const float eOld = 0.5f * (barEnergy (cur - 3) + barEnergy (cur - 4));
    f.energyTrend = std::max (-1.f, std::min (1.f, (f.energy - eOld) * 2.f));
    int onsets = 0, hi = 0, off16 = 0, trip = 0, uni = 0, bars = 0;
    for (int64_t b = cur - 2; b <= cur - 1; ++b)
        if (const auto* s = find (b))
        {
            onsets += s->onsets; hi += s->highTotal; off16 += s->highOff16; trip += s->highTrip; uni += s->unison; ++bars;
        }
    if (bars > 0)
    {
        f.density = std::min (1.f, (float) onsets / (float) (bars * beatsPerBar * 3));
        f.sixteenthFeel = hi > 0 ? std::min (1.f, 2.5f * (float) off16 / (float) hi) : 0.f;
        f.tripletFeel = hi > 0 ? std::min (1.f, 2.5f * (float) trip / (float) hi) : 0.f;
        f.unison = std::min (1.f, (float) uni / (float) (bars * 2));
    }
    f.novelty = std::max (noveltyOf (cur - 1), noveltyOf (cur) * 0.8f);
    return f;
}

bool ListeningState::hasRecentData (double nowPpq) const
{
    return (nowPpq - lastOnsetPpq) < (double) beatsPerBar * 4.0 || (nowPpq - lastEnergyPpq) < 2.0;
}

int ListeningState::onsetsInRange (double a, double b, OnsetRecord* out, int maxOut) const
{
    int n = 0;
    for (int i = 0; i < recentCount && n < maxOut; ++i)
    {
        const auto& r = recent[(size_t) ((recentHead - 1 - i + kRecent) % kRecent)];
        if (r.ppq < a) break; // records are chronological
        if (r.ppq < b) out[n++] = r;
    }
    return n;
}
} // namespace hammer
