#pragma once
// ============================================================================
//  SharedLink - lock-free bridge between "The Hammer Ears" (writer) and
//  "The Hammer" drummer (reader). Uses named shared memory so it works even
//  though they are separate plugin binaries (and even if FL Studio bridges
//  them into separate processes). 8 link channels so several Ears/drummer
//  pairs can run in one project.
// ============================================================================
#include <atomic>
#include <cstdint>
#include <cstring>

namespace hammer
{
struct LinkEvent
{
    double ppq;
    float band[3];
    float strength;
    uint32_t kind; // 0 = onset, 1 = energy frame (band = dBFS values)
    uint32_t pad;
};

struct LinkBlock
{
    static constexpr uint32_t kMagic = 0x484D5231; // "HMR1"
    static constexpr int kEvents = 2048;
    std::atomic<uint32_t> magic;
    std::atomic<uint32_t> version;
    std::atomic<uint64_t> writeSeq;
    std::atomic<int64_t> heartbeatMs;
    std::atomic<uint64_t> bpmBits;
    std::atomic<uint64_t> ppqBits;
    std::atomic<uint32_t> playing;
    std::atomic<uint32_t> meterBits; // low byte = beats per bar
    float meters[3];
    LinkEvent events[kEvents];
};

class SharedLink
{
public:
    SharedLink() = default;
    ~SharedLink();

    bool open (int channel); // 1..8, creates if needed
    void close();
    bool isOpen() const { return block != nullptr; }
    int channel() const { return currentChannel; }

    // Writer side (Ears)
    void publishEvent (const LinkEvent& e);
    void publishTransport (double ppq, double bpm, bool playing, int beatsPerBar, const float meters[3]);

    // Reader side (Drummer). Returns number of new events copied.
    int poll (LinkEvent* out, int maxOut);
    bool writerAlive (int64_t timeoutMs = 1500) const;
    void readMeters (float out[3]) const;
    double writerBpm() const;

    static int64_t nowMs();

private:
    LinkBlock* block = nullptr;
    void* handle = nullptr;
    int fd = -1;
    int currentChannel = 0;
    uint64_t readSeq = 0;
};
} // namespace hammer
