// Host (PC) implementation of nn::os — the platform stand-in for the boot milestone.
//
// Every function here exists because `main` imports it; the implementations are new,
// because no vendor implementation is present in the game dump. Frequencies and
// argument shapes in the comments come from the whole-binary call-site scan.
//
// Design notes for the reconstructed semantics:
//   * GetSystemTick must be monotonic and cheap — it is the single hottest import at
//     677 call sites, and `printFromStartTime`/`Actor`/`syncCacheStorageImpl` all use it
//     as a wall-clock for elapsed-time measurement, so it must never go backwards.
//   * LockMutex/UnlockMutex are embedded by value in game objects, so the host-backed
//     storage must be self-contained and trivially relocatable is NOT required — the
//     game only ever hands us the address it allocated.
//   * TLS slots are a fixed small pool; the SDK allows 16.
#include "nn/os.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <string>
#include <thread>

#if !defined(_WIN32)
#  include <pthread.h>
#endif

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace nn::os {
namespace {

// ---- tick ---------------------------------------------------------------
// Convert a monotonic clock reading to the 19.2 MHz tick domain. Computed with integer
// arithmetic so the result is exact and monotonic rather than drifting on a double.
std::uint64_t NowTicks() {
    using clock = std::chrono::steady_clock;
    static const auto origin = clock::now();
    const auto delta = clock::now() - origin;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count();
    if (ns < 0) return 0;
    // ticks = ns * 19'200'000 / 1'000'000'000 = ns * 192 / 10000
    return static_cast<std::uint64_t>(ns) * 192ull / 10000ull;
}

// ---- TLS ----------------------------------------------------------------
// The SDK provides 16 TLS slots. Storage is per-thread, so a thread_local array is the
// faithful host mapping — GetTlsValue/SetTlsValue are called from many threads and must
// not share state between them.
constexpr std::uint32_t kTlsSlotCount = 16;
thread_local void *g_tls[kTlsSlotCount] = {};
std::atomic<std::uint32_t> g_tlsNext{0};
TlsDestructor g_tlsDestructor[kTlsSlotCount] = {};

// ---- thread handles -----------------------------------------------------
// The game stores an opaque handle and uses it as an identity token — the observed code
// paths never dereference it. A stable per-thread id is therefore faithful.
thread_local std::uint32_t g_threadId = 0;
std::atomic<std::uint32_t> g_nextThreadId{1};
std::mutex g_threadNamesLock;
std::map<std::uint32_t, std::string> g_threadNames;

std::uint32_t CurrentThreadId() {
    if (g_threadId == 0) g_threadId = g_nextThreadId.fetch_add(1);
    return g_threadId;
}

// ---- memory -------------------------------------------------------------
// AllocateMemoryBlock observably rounds up to 2 MB at a call site
// (`param_1 + 0x1fffffU & 0xffffffffffe00000`) and requests are large (0x2000000 =
// 32 MB observed), so page-aligned host allocation matches the intent.
constexpr std::uint64_t kAllocGranularity = 0x200000; // 2 MB

// ---- mutex --------------------------------------------------------------
// The game allocates storage inline and hands us the pointer, e.g.
// `LockMutex((MutexType *)(this + 0x348))`. The actual host mutex is heap-allocated and
// MutexType::storage holds only the pointer — same pattern used for SemaphoreType above.
// This keeps MutexType::storage small regardless of how large the host mutex is on any
// given compiler (MSVC x64: std::recursive_mutex is ~80 bytes; GCC x64: ~40 bytes).
struct MutexState {
    bool isRecursive;
    union {
        std::mutex plain;
        std::recursive_mutex recursive;
    };
    explicit MutexState(bool rec) : isRecursive(rec) {
        if (rec) new (&this->recursive) std::recursive_mutex();
        else new (&plain) std::mutex();
    }
    ~MutexState() {
        if (isRecursive) this->recursive.~recursive_mutex();
        else plain.~mutex();
    }
};

struct MutexStorage {
    MutexState *state;
};

MutexState *AsStorage(MutexType *m) {
    return reinterpret_cast<MutexStorage *>(m->storage)->state;
}

} // namespace

// ---- time ---------------------------------------------------------------

std::uint64_t GetSystemTick() { return NowTicks(); }

void SleepThread(std::int64_t nanoseconds) {
    if (nanoseconds <= 0) {
        std::this_thread::yield();
        return;
    }
    std::this_thread::sleep_for(std::chrono::nanoseconds(nanoseconds));
}

std::uint64_t ConvertToTimeSpan(std::int64_t nanoseconds) {
    // nn::TimeSpan is nanoseconds in the SDK; the call sites feed the result straight
    // into comparisons against GetSystemTick output, so convert to ticks.
    if (nanoseconds <= 0) return 0;
    return static_cast<std::uint64_t>(nanoseconds) * 192ull / 10000ull;
}

// ---- TLS ----------------------------------------------------------------

void *GetTlsValue(std::uint32_t slot) {
    if (slot >= kTlsSlotCount) return nullptr;
    return g_tls[slot];
}

void SetTlsValue(std::uint32_t slot, void *value) {
    if (slot >= kTlsSlotCount) return;
    g_tls[slot] = value;
}

int AllocateTlsSlot(std::uint32_t *outSlot, TlsDestructor destructor) {
    if (outSlot == nullptr) return -1;
    const std::uint32_t slot = g_tlsNext.fetch_add(1);
    if (slot >= kTlsSlotCount) return -1;
    g_tlsDestructor[slot] = destructor;
    *outSlot = slot;
    return 0;
}

// ---- memory -------------------------------------------------------------

int AllocateMemoryBlock(std::uint64_t *outAddress, std::uint64_t size) {
    if (outAddress == nullptr) return -1;
    *outAddress = 0;
    if (size == 0) return -1;
    const std::uint64_t rounded = (size + kAllocGranularity - 1) & ~(kAllocGranularity - 1);
    void *p = nullptr;
#if defined(_WIN32)
    p = _aligned_malloc(static_cast<std::size_t>(rounded), 4096);
#else
    if (posix_memalign(&p, 4096, static_cast<std::size_t>(rounded)) != 0) p = nullptr;
#endif
    if (p == nullptr) return -1;
    std::memset(p, 0, static_cast<std::size_t>(rounded));
    *outAddress = reinterpret_cast<std::uint64_t>(p);
    return 0;
}

void QueryMemoryInfo(MemoryInfo *out) {
    if (out == nullptr) return;
    // Used during boot to size heaps and print the "%4ld MB: Program" diagnostics seen
    // at LogImpl call sites. Report a plausible Switch budget rather than the host's, so
    // heap-sizing logic sees the platform it was written for.
    constexpr std::uint64_t kSwitchTotalMemory = 4ull * 1024 * 1024 * 1024;
    out->totalSize = kSwitchTotalMemory;
    out->usedSize = 0;
}

void SetMemoryHeapSize(std::uint64_t /*size*/) {
    // The standalone-heap budget. The host allocator does not need it; recorded as a
    // no-op rather than pretending to enforce a limit.
}

// ---- synchronisation ----------------------------------------------------

void InitializeMutex(MutexType *mutex, bool recursive, int /*priorityInheritance*/) {
    if (mutex == nullptr) return;
    static_assert(sizeof(MutexStorage) <= sizeof(MutexType::storage),
                  "MutexType storage too small for a MutexState pointer");
    new (mutex->storage) MutexStorage{new MutexState(recursive)};
}

void LockMutex(MutexType *mutex) {
    if (mutex == nullptr) return;
    auto *s = AsStorage(mutex);
    if (s->isRecursive) s->recursive.lock();
    else s->plain.lock();
}

void UnlockMutex(MutexType *mutex) {
    if (mutex == nullptr) return;
    auto *s = AsStorage(mutex);
    if (s->isRecursive) s->recursive.unlock();
    else s->plain.unlock();
}

// The semaphore was previously a no-op, which is the most dangerous stub in this file: a
// no-op Acquire never blocks and a no-op Release never wakes, so any code that waits on
// one would either spin forever or proceed without its precondition being met. The boot
// analysis in docs/BOOT_CALLGRAPH.md flagged `RootTask::waitDoneParallelInitThread` as a
// likely spin-wait site, and it uses exactly these primitives.
//
// STORAGE HOLDS A POINTER, NOT THE STATE. An in-place version sized the buffer from
// x86-64 layout, passed the static_assert there, then FAILED on AArch64 where the same
// state measures 104 bytes instead of 96. Keeping the SDK-visible type small and holding
// host state behind one pointer makes the layout ABI-independent -- the SDK only ever
// receives a pointer to caller-owned storage, so it never needs to know the host size.
struct SemaphoreState {
    std::mutex lock;
    std::condition_variable cv;
    int count;
    int maxCount;
    SemaphoreState(int initial, int max) : count(initial), maxCount(max) {}
};

struct SemaphoreStorage {
    SemaphoreState *state;
};

SemaphoreStorage *AsSemaphore(SemaphoreType *s) {
    return reinterpret_cast<SemaphoreStorage *>(s->storage);
}

void InitializeSemaphore(SemaphoreType *semaphore, int initial, int max) {
    if (semaphore == nullptr) return;
    static_assert(sizeof(SemaphoreStorage) <= sizeof(SemaphoreType::storage),
                  "SemaphoreType storage too small for the host state pointer");
    auto *s = AsSemaphore(semaphore);
    s->state = nullptr;
    // The SDK rejects an initial count above the maximum; mirror that rather than
    // silently constructing an unsatisfiable semaphore.
    if (initial < 0 || max <= 0 || initial > max) return;
    s->state = new (std::nothrow) SemaphoreState(initial, max);
}

void AcquireSemaphore(SemaphoreType *semaphore) {
    if (semaphore == nullptr) return;
    auto *s = AsSemaphore(semaphore);
    if (s->state == nullptr) return;
    std::unique_lock<std::mutex> g(s->state->lock);
    s->state->cv.wait(g, [s] { return s->state->count > 0; });
    --s->state->count;
}

void ReleaseSemaphore(SemaphoreType *semaphore) {
    if (semaphore == nullptr) return;
    auto *s = AsSemaphore(semaphore);
    if (s->state == nullptr) return;
    std::lock_guard<std::mutex> g(s->state->lock);
    // Releasing past the maximum is a caller error; refraining from incrementing keeps
    // the count inside the declared range instead of manufacturing a permit.
    if (s->state->count >= s->state->maxCount) return;
    ++s->state->count;
    s->state->cv.notify_one();
}

void InitializeMessageQueue(MessageQueueType *queue, std::uint64_t *buffer, std::size_t count) {
    if (queue == nullptr) return;
    std::memset(queue->storage, 0, sizeof(queue->storage));
    if (buffer != nullptr) std::memset(buffer, 0, count * sizeof(std::uint64_t));
}

// ---- CPU / threads ------------------------------------------------------

std::uint32_t GetCurrentCoreNumber() {
    // Callers use this to pick per-core work buffers, so any stable in-range value works.
    // std::thread::hardware_concurrency is the host analogue but does not identify the
    // *current* core, so report 0 deterministically rather than a misleading value.
    return 0;
}

std::uint64_t GetThreadAvailableCoreMask() {
    const unsigned n = std::thread::hardware_concurrency();
    // The Switch has 4 cores. Cap the mask so game code sizing per-core arrays stays
    // inside the range its data structures expect.
    const unsigned usable = n == 0 ? 1u : (n > 4u ? 4u : n);
    return (1ull << usable) - 1ull;
}

void SetThreadCoreMask(ThreadType *, int, std::uint64_t) {}
void ChangeThreadPriority(ThreadType *, int) {}

std::uint32_t GetCurrentThread() { return CurrentThreadId(); }

std::uint64_t GetThreadId(ThreadType *thread) {
    // A ThreadType the game created is identified by its storage address; a null pointer
    // means "the current thread".
    if (thread == nullptr) return CurrentThreadId();
    return reinterpret_cast<std::uint64_t>(thread);
}

int GetThreadPriority(ThreadType *) {
    // The SDK's default priority. Callers compare and restore it, so a stable value is
    // required; they do not act on the magnitude.
    return 0x20;
}

void SetThreadName(ThreadType *thread, const char *name) {
    if (name == nullptr) return;
    const std::uint32_t id = thread == nullptr ? CurrentThreadId()
                                               : static_cast<std::uint32_t>(
                                                     reinterpret_cast<std::uintptr_t>(thread));
    std::lock_guard<std::mutex> g(g_threadNamesLock);
    g_threadNames[id] = name;
}

void GetCurrentStackInfo(std::uintptr_t *outBase, std::uintptr_t *outSize) {
    if (outBase != nullptr) *outBase = 0;
    if (outSize != nullptr) *outSize = 0;
#if defined(_WIN32)
    ULONG_PTR lowLimit = 0, highLimit = 0;
    GetCurrentThreadStackLimits(&lowLimit, &highLimit);
    if (outBase != nullptr) *outBase = static_cast<std::uintptr_t>(lowLimit);
    if (outSize != nullptr) *outSize = static_cast<std::uintptr_t>(highLimit - lowLimit);
#else
    pthread_attr_t attr;
    if (pthread_getattr_np(pthread_self(), &attr) == 0) {
        void *addr = nullptr;
        std::size_t size = 0;
        if (pthread_attr_getstack(&attr, &addr, &size) == 0) {
            if (outBase != nullptr) *outBase = reinterpret_cast<std::uintptr_t>(addr);
            if (outSize != nullptr) *outSize = static_cast<std::uintptr_t>(size);
        }
        pthread_attr_destroy(&attr);
    }
#endif
}


// --- nn::os system events -----------------------------------------------------------------
// Needed by nn::vi::GetDisplayVsyncEvent: the engine's render loop waits on a per-frame event,
// so the event has to be real rather than a null the loop would fault on. EventState is heap-
// allocated and EventType::storage holds only the pointer, for the same reason MutexStorage
// uses a pointer: std::mutex + std::condition_variable together exceed 128 bytes on MSVC x64.
namespace {
struct EventState {
    std::mutex m;
    std::condition_variable cv;
    bool signaled;
    bool autoClear;
};

struct EventStorage {
    EventState *state;
};

EventState *EventOf(nn::os::SystemEventType *e) {
    static_assert(sizeof(EventStorage) <= sizeof(nn::os::EventType::storage),
                  "EventType::storage is too small for an EventState pointer");
    return reinterpret_cast<EventStorage *>(e->storage)->state;
}
} // namespace

int CreateSystemEvent(SystemEventType *outEvent, EventClearMode clearMode, bool initiallySignaled) {
    if (outEvent == nullptr) return -1;
    EventState *s = new EventState();
    s->signaled = initiallySignaled;
    s->autoClear = (clearMode == EventClearMode::AutoClear);
    new (outEvent->storage) EventStorage{s};
    return 0;
}

void SignalSystemEvent(SystemEventType *event) {
    if (event == nullptr) return;
    EventState *s = EventOf(event);
    {
        std::lock_guard<std::mutex> lock(s->m);
        s->signaled = true;
    }
    s->cv.notify_all();
}

void ClearSystemEvent(SystemEventType *event) {
    if (event == nullptr) return;
    EventState *s = EventOf(event);
    std::lock_guard<std::mutex> lock(s->m);
    s->signaled = false;
}

void WaitSystemEvent(SystemEventType *event) {
    if (event == nullptr) return;
    EventState *s = EventOf(event);
    std::unique_lock<std::mutex> lock(s->m);
    s->cv.wait(lock, [s] { return s->signaled; });
    // AutoClear: consuming the signal resets it, which is what a per-frame vsync wait means.
    if (s->autoClear) s->signaled = false;
}
} // namespace nn::os
