#pragma once
// ============================================================================
//  SampleKit - multisampled drum kits from disk.
//
//  Formats:
//   * Hammer kit folder:  MyKit/kit.json (optional) + one sub-folder per drum
//       (kick, snare, rim, hihat_closed, hihat_open, hihat_pedal, tom_high,
//        tom_mid, tom_floor, crash, crash2, ride, ride_bell, china, splash,
//        clap, cowbell, shaker, tambourine, perc). Drop WAV/FLAC/AIFF/OGG in;
//       velocity layers + round robins are detected from names (_v3, _rr2,
//       pp/mf/ff ...) or by measuring loudness.
//   * SFZ (subset): <control>/<global>/<master>/<group>/<region>, #define,
//       #include, sample, key/lokey/hikey, lovel/hivel, seq_position, lorand,
//       volume, pan, amplitude, default_path. Multi-mic regions that share a
//       key/velocity/round-robin are mixed into one stereo hit at load time.
// ============================================================================
#include "../Common/HammerTypes.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <string>

namespace hammer
{
struct SampleLayer
{
    float loudness = 0.f;                       // peak short-term RMS
    std::vector<juce::AudioBuffer<float>> rr;   // round robins (stereo)
};

struct SampleLane
{
    std::vector<SampleLayer> layers;            // quiet -> loud
    float gain = 1.f, pan = 0.f;
    bool present() const { return ! layers.empty(); }
};

struct SampleKitData
{
    juce::String name, author, license, credits, sourcePath;
    juce::StringArray styles;
    std::vector<std::string> styleTags; // same as 'styles', for allocation-free checks on the audio thread
    bool hasTag (const char* t) const { for (auto& s : styleTags) if (s == t) return true; return false; }
    SampleLane lanes[NumLanes];
    double sampleRate = 44100.0;
    size_t bytes = 0;
    int numFiles = 0;
};

struct KitInfo
{
    juce::String name, path, license, author;
    juce::StringArray styles;
};

class SampleKitLoader
{
public:
    static std::unique_ptr<SampleKitData> load (const juce::File& source, double targetRate,
                                                juce::String& error, const std::atomic<bool>* cancel = nullptr);
    static juce::File defaultKitsFolder();
    static std::vector<KitInfo> scan (const juce::File& folder);
    static int laneForFolderName (const juce::String& name);
};

// Four kit slots, loaded on a background thread; the audio thread just reads
// an atomic pointer. Replaced kits are retired and freed later (never while
// the audio thread could still be using them).
class KitSlots : private juce::Thread
{
public:
    static constexpr int kNumSlots = 4;
    KitSlots();
    ~KitSlots() override;

    void setSampleRate (double sr);
    void requestLoad (int slot, const juce::File& source);
    void clearSlot (int slot);
    SampleKitData* get (int slot) const { return (slot >= 0 && slot < kNumSlots) ? current[slot].load (std::memory_order_acquire) : nullptr; }
    juce::String pathOf (int slot) const;
    juce::String statusOf (int slot) const;
    bool isLoading (int slot) const;
    // find a loaded or available kit matching a style tag; returns slot or -1
    int slotWithTag (const char* tag) const;
    void freeRetired(); // call from message thread occasionally

private:
    void run() override;

    std::atomic<SampleKitData*> current[kNumSlots];
    std::vector<std::unique_ptr<SampleKitData>> owned[kNumSlots]; // [0] is current, older ones retired
    std::vector<std::pair<juce::int64, std::unique_ptr<SampleKitData>>> retired;
    juce::String requested[kNumSlots], loadedPath[kNumSlots], status[kNumSlots];
    bool pending[kNumSlots] {};
    std::atomic<bool> loading[kNumSlots];
    mutable std::mutex lock;
    double rate = 44100.0;
    std::atomic<bool> cancelFlag { false };
    juce::WaitableEvent wake;
};
} // namespace hammer
