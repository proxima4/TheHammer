#include "MidiRecorder.h"
#include <algorithm>
#include <vector>
#include <cmath>

namespace hammer
{
void MidiRecorder::add (double ppq, int lane, float velocity)
{
    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked()) return;
    ring[head] = { ppq, (uint8_t) laneToGmNote (lane), (uint8_t) juce::jlimit (1, 127, (int) std::lround (velocity * 127.f)) };
    head = (head + 1) % kMax;
    count = std::min (count + 1, kMax);
}

void MidiRecorder::clear()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    head = count = 0;
}

int MidiRecorder::numEvents() const { return count; }

bool MidiRecorder::exportLastBars (const juce::File& file, int bars, int bpb, double bpm) const
{
    std::vector<Ev> evs;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (int i = 0; i < count; ++i) evs.push_back (ring[(head - count + i + kMax) % kMax]);
    }
    if (evs.empty()) return false;
    std::sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.ppq < b.ppq; });
    const double last = evs.back().ppq;
    const double endBar = std::ceil ((last + 1e-6) / bpb) * bpb;
    const double startPpq = std::max (0.0, endBar - (double) bars * bpb);

    constexpr int ppqRes = 960;
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / std::max (20.0, bpm))), 0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (bpb, 4), 0);
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "The Hammer by APT Industrial"), 0);
    for (const auto& e : evs)
    {
        if (e.ppq < startPpq) continue;
        const double t = (e.ppq - startPpq) * ppqRes;
        seq.addEvent (juce::MidiMessage::noteOn (10, e.note, (juce::uint8) e.vel), std::max (0.0, t));
        seq.addEvent (juce::MidiMessage::noteOff (10, e.note), std::max (0.0, t) + ppqRes / 8.0);
    }
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppqRes);
    mf.addTrack (seq);
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk()) return false;
    return mf.writeTo (out);
}
} // namespace hammer
