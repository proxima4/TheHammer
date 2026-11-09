#include "SharedLink.h"
#include <chrono>
#include <cstdio>
#include <new>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <fcntl.h>
 #include <unistd.h>
#endif

namespace hammer
{
static_assert (std::atomic<uint64_t>::is_always_lock_free, "need lock-free 64-bit atomics for cross-process link");

int64_t SharedLink::nowMs()
{
    using namespace std::chrono;
    return (int64_t) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
}

SharedLink::~SharedLink() { close(); }

bool SharedLink::open (int ch)
{
    if (ch < 1) ch = 1;
    if (ch > 8) ch = 8;
    if (block != nullptr && ch == currentChannel) return true;
    close();
    const size_t size = sizeof (LinkBlock);
#if defined(_WIN32)
    wchar_t name[128];
    swprintf (name, 128, L"Local\\APTIndustrial_TheHammer_Link_%d", ch);
    HANDLE h = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) size, name);
    if (h == nullptr) return false;
    void* p = MapViewOfFile (h, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (p == nullptr) { CloseHandle (h); return false; }
    handle = h;
    block = static_cast<LinkBlock*> (p);
#else
    char name[64];
    std::snprintf (name, sizeof (name), "/apt_thehammer_link_%d", ch);
    int f = shm_open (name, O_CREAT | O_RDWR, 0600);
    if (f < 0) return false;
    struct stat st {};
    if (fstat (f, &st) == 0 && (size_t) st.st_size < size)
        if (ftruncate (f, (off_t) size) != 0) { ::close (f); return false; }
    void* p = mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, f, 0);
    if (p == MAP_FAILED) { ::close (f); return false; }
    fd = f;
    block = static_cast<LinkBlock*> (p);
#endif
    // Fresh mapping is zero-filled; zero is a valid state for all atomics.
    if (block->magic.load() != LinkBlock::kMagic)
    {
        block->version.store (1);
        block->magic.store (LinkBlock::kMagic);
    }
    currentChannel = ch;
    readSeq = block->writeSeq.load (std::memory_order_acquire);
    return true;
}

void SharedLink::close()
{
    if (block == nullptr) return;
#if defined(_WIN32)
    UnmapViewOfFile (block);
    CloseHandle ((HANDLE) handle);
    handle = nullptr;
#else
    munmap (block, sizeof (LinkBlock));
    ::close (fd);
    fd = -1;
#endif
    block = nullptr;
    currentChannel = 0;
}

void SharedLink::publishEvent (const LinkEvent& e)
{
    if (block == nullptr) return;
    const uint64_t seq = block->writeSeq.load (std::memory_order_relaxed);
    block->events[seq % LinkBlock::kEvents] = e;
    block->writeSeq.store (seq + 1, std::memory_order_release);
}

void SharedLink::publishTransport (double ppq, double bpm, bool playing, int bpb, const float meters[3])
{
    if (block == nullptr) return;
    uint64_t b; std::memcpy (&b, &bpm, 8); block->bpmBits.store (b, std::memory_order_relaxed);
    uint64_t p; std::memcpy (&p, &ppq, 8); block->ppqBits.store (p, std::memory_order_relaxed);
    block->playing.store (playing ? 1u : 0u, std::memory_order_relaxed);
    block->meterBits.store ((uint32_t) bpb, std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i) block->meters[i] = meters[i];
    block->heartbeatMs.store (nowMs(), std::memory_order_release);
}

int SharedLink::poll (LinkEvent* out, int maxOut)
{
    if (block == nullptr) return 0;
    const uint64_t w = block->writeSeq.load (std::memory_order_acquire);
    if (w < readSeq) readSeq = w; // writer restarted
    if (w - readSeq > (uint64_t) LinkBlock::kEvents) readSeq = w - LinkBlock::kEvents;
    int n = 0;
    while (readSeq < w && n < maxOut)
        out[n++] = block->events[readSeq++ % LinkBlock::kEvents];
    return n;
}

bool SharedLink::writerAlive (int64_t timeoutMs) const
{
    if (block == nullptr) return false;
    const int64_t hb = block->heartbeatMs.load (std::memory_order_acquire);
    return hb != 0 && (nowMs() - hb) < timeoutMs;
}

void SharedLink::readMeters (float out[3]) const
{
    for (int i = 0; i < 3; ++i) out[i] = block ? block->meters[i] : 0.f;
}

double SharedLink::writerBpm() const
{
    if (block == nullptr) return 0.0;
    const uint64_t b = block->bpmBits.load (std::memory_order_relaxed);
    double d; std::memcpy (&d, &b, 8);
    return d;
}
} // namespace hammer
