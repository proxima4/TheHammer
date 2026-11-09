#pragma once
// Keeps the last few minutes of what The Hammer played so you can drag it into
// FL Studio's playlist / piano roll as a MIDI file.
#include "../Common/HammerTypes.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace hammer
{
class MidiRecorder
{
public:
    void add (double ppq, int lane, float velocity);
    void clear();
    // Writes the last 'bars' bars (ending at the most recent bar line) to a .mid file.
    bool exportLastBars (const juce::File& file, int bars, int beatsPerBar, double bpm) const;
    int numEvents() const;

private:
    struct Ev { double ppq; uint8_t note, vel; };
    static constexpr int kMax = 16384;
    Ev ring[kMax];
    int head = 0, count = 0;
    mutable juce::SpinLock lock;
};
} // namespace hammer
