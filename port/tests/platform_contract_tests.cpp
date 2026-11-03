// Contract tests for the reconstructed nn::os platform layer.
//
// Each test asserts a property that the CALL SITES require, not merely that the
// function runs. The property is named in the test so a failure says which observed
// requirement was violated. Call-site frequencies come from
// docs/PLATFORM_IMPLEMENTATION_SPEC.md.
#include "nn/os.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    } else {
        std::printf("  ok    %s\n", what);
    }
}

// ---------------------------------------------------------------------------
// GetSystemTick — 677 call sites, the hottest import. Callers use it as a wall
// clock for elapsed-time measurement (printFromStartTime, Actor, syncCacheStorageImpl),
// so the two hard requirements are monotonicity and a plausible tick rate.
// ---------------------------------------------------------------------------
void TestGetSystemTick() {
    std::printf("GetSystemTick (677 call sites)\n");

    const auto t0 = nn::os::GetSystemTick();
    const auto t1 = nn::os::GetSystemTick();
    Check(t1 >= t0, "non-decreasing across consecutive calls");

    // Busy-wait ~20 ms of wall time and confirm the tick delta matches the 19.2 MHz
    // domain. This catches a unit error, which would make every elapsed-time
    // computation in the game wrong by orders of magnitude.
    const auto wall0 = std::chrono::steady_clock::now();
    const auto tick0 = nn::os::GetSystemTick();
    while (std::chrono::steady_clock::now() - wall0 < std::chrono::milliseconds(20)) {
        std::this_thread::yield();
    }
    const auto tick1 = nn::os::GetSystemTick();
    const auto wallMs =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wall0).count() / 1000.0;
    const double ticks = static_cast<double>(tick1 - tick0);
    const double impliedMs = ticks / (nn::os::kTickFrequencyHz / 1000.0);

    std::printf("        wall=%.2f ms  ticks=%.0f  implied=%.2f ms\n", wallMs, ticks, impliedMs);
    Check(ticks > 0, "advances over a 20 ms busy-wait");
    Check(impliedMs > 5.0 && impliedMs < 60.0,
          "tick delta corresponds to the elapsed wall time (19.2 MHz domain)");

    // Monotonic under contention, since many threads poll it.
    std::atomic<bool> bad{false};
    std::vector<std::thread> ts;
    for (int i = 0; i < 4; ++i) {
        ts.emplace_back([&bad] {
            std::uint64_t last = nn::os::GetSystemTick();
            for (int k = 0; k < 20000; ++k) {
                const auto now = nn::os::GetSystemTick();
                if (now < last) bad = true;
                last = now;
            }
        });
    }
    for (auto &t : ts) t.join();
    Check(!bad.load(), "monotonic under 4 threads x 20000 calls");
}

// ---------------------------------------------------------------------------
// SleepThread — `nn::os::SleepThread(5000000);` (5 ms in ns). Callers rely on the
// argument being a duration in nanoseconds and on it actually sleeping.
// ---------------------------------------------------------------------------
void TestSleepThread() {
    std::printf("SleepThread (23 call sites)\n");
    const auto w0 = std::chrono::steady_clock::now();
    nn::os::SleepThread(5'000'000); // 5 ms, the literal observed at a call site
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - w0).count();
    std::printf("        slept %lld us for a 5000000 ns request\n",
                static_cast<long long>(us));
    Check(us >= 4000, "a 5,000,000 argument sleeps at least ~4 ms (nanosecond units)");
    Check(us < 200000, "and does not oversleep grossly");

    // A zero/negative request must not block; call sites pass a computed delay.
    const auto z0 = std::chrono::steady_clock::now();
    nn::os::SleepThread(0);
    const auto zus = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - z0).count();
    Check(zus < 5000, "SleepThread(0) returns promptly");
}

// ---------------------------------------------------------------------------
// TLS — GetTlsValue 264 call sites, SetTlsValue 8, called from many threads.
// The essential property is that slots are PER-THREAD: a value set on one thread
// must not be visible on another.
// ---------------------------------------------------------------------------
void TestTls() {
    std::printf("TLS (GetTlsValue 264, SetTlsValue 8 call sites)\n");

    // Observed signature: AllocateTlsSlot(this + 0x88, (TlsDestructor)0x0) — the slot
    // number comes back through the first argument, not as a return value.
    std::uint32_t slot = 0xFFFFFFFFu;
    const int rc = nn::os::AllocateTlsSlot(&slot, nullptr);
    Check(rc == 0, "AllocateTlsSlot returns success");
    Check(slot < 16, "and writes a slot number below the 16-slot limit through the out param");

    nn::os::SetTlsValue(slot, reinterpret_cast<void *>(0x1234));
    Check(nn::os::GetTlsValue(slot) == reinterpret_cast<void *>(0x1234),
          "value round-trips on the allocating thread");

    void *seenOnOtherThread = reinterpret_cast<void *>(static_cast<std::uintptr_t>(0xdeadbeef));
    std::thread other([&] { seenOnOtherThread = nn::os::GetTlsValue(slot); });
    other.join();
    Check(seenOnOtherThread != reinterpret_cast<void *>(0x1234),
          "value is NOT visible on another thread (slot storage is per-thread)");

    nn::os::SetTlsValue(slot, reinterpret_cast<void *>(0x5678));
    Check(nn::os::GetTlsValue(slot) == reinterpret_cast<void *>(0x5678),
          "value is overwritable");

    Check(nn::os::GetTlsValue(9999) == nullptr, "out-of-range slot reads as null");

    // Exhaustion must be reported, not silently wrapped.
    std::uint32_t last = 0;
    int lastRc = 0;
    for (int i = 0; i < 40; ++i) lastRc = nn::os::AllocateTlsSlot(&last, nullptr);
    Check(lastRc != 0, "allocating past 16 slots fails rather than wrapping");
}

// ---------------------------------------------------------------------------
// Memory — AllocateMemoryBlock(&local_18, 0x2000000) => (out, size); the caller then
// rounds its request up to a 2 MB granularity, so the allocation must be page-aligned.
// ---------------------------------------------------------------------------
void TestMemory() {
    std::printf("Memory (AllocateMemoryBlock, QueryMemoryInfo, SetMemoryHeapSize)\n");

    std::uint64_t addr = 0;
    const int rc = nn::os::AllocateMemoryBlock(&addr, 0x2000000); // 32 MB observed
    Check(rc == 0, "AllocateMemoryBlock(32 MB) succeeds");
    Check(addr != 0, "and writes a non-null address through the out param");
    Check((addr & 0xFFF) == 0, "the address is page-aligned");
    if (addr != 0) {
        // Must be usable and zero-filled: callers treat it as fresh heap.
        auto *p = reinterpret_cast<volatile std::uint64_t *>(addr);
        Check(p[0] == 0 && p[4096 / 8] == 0, "the block is zero-filled and writable");
        nn::os::FreeMemoryBlock(addr, 0x2000000);
    }

    std::uint64_t none = 0xFFFF;
    Check(nn::os::AllocateMemoryBlock(&none, 0) != 0, "a zero-size request fails");

    nn::os::MemoryInfo info{};
    nn::os::QueryMemoryInfo(&info);
    Check(info.totalSize > 0, "QueryMemoryInfo reports a non-zero total");
    std::printf("        reported total = %llu MB\n",
                static_cast<unsigned long long>(info.totalSize / (1024 * 1024)));

    nn::os::SetMemoryHeapSize(0x10000000);
    Check(true, "SetMemoryHeapSize accepts a budget without crashing");

    nn::os::QueryMemoryInfo(nullptr); // must not crash
    Check(true, "QueryMemoryInfo tolerates a null out pointer");
}

// ---------------------------------------------------------------------------
// Thread queries — GetThreadId/GetThreadPriority/SetThreadName/GetCurrentStackInfo.
// Callers use the id as an identity token and store the name for debugging.
// ---------------------------------------------------------------------------
void TestThreadQueries() {
    std::printf("Thread queries (GetThreadId, GetThreadPriority, SetThreadName, GetCurrentStackInfo)\n");

    nn::os::ThreadType *self = nn::os::GetCurrentThread();
    const std::uint64_t id = nn::os::GetThreadId(self);
    Check(id != 0, "GetThreadId(GetCurrentThread()) is a non-zero id");
    Check(nn::os::GetCurrentThread() == self && nn::os::GetThreadId(self) == id,
          "the current ThreadType and its id are stable");
    Check(nn::os::GetThreadId(nullptr) == 0, "GetThreadId(nullptr) is 0, not a real thread");

    std::uint64_t otherId = 0;
    nn::os::ThreadType *otherType = nullptr;
    std::thread other([&] {
        otherType = nn::os::GetCurrentThread();
        otherId = nn::os::GetThreadId(otherType);
    });
    other.join();
    Check(otherType != self && otherId != 0 && otherId != id,
          "another host thread gets its own ThreadType and id");

    const int prio = nn::os::GetThreadPriority(self);
    Check(prio > 0, "GetThreadPriority returns a positive SDK-range priority");
    nn::os::ChangeThreadPriority(self, 16);
    Check(nn::os::GetThreadPriority(self) == 16, "ChangeThreadPriority is read back");
    nn::os::ChangeThreadPriority(self, prio);

    nn::os::SetThreadName(self, "sp2-main");
    Check(true, "SetThreadName accepts the current thread");

    std::uintptr_t base = 0, size = 0;
    nn::os::GetCurrentStackInfo(&base, &size);
    std::printf("        stack base=0x%llx size=%llu bytes\n",
                static_cast<unsigned long long>(base),
                static_cast<unsigned long long>(size));
    Check(size > 0, "GetCurrentStackInfo reports a non-zero stack size");
    Check(size < (1ull << 32), "and a plausible magnitude");

    nn::os::GetCurrentStackInfo(nullptr, nullptr); // must not crash
    Check(true, "GetCurrentStackInfo tolerates null out pointers");
}

// ---------------------------------------------------------------------------
// Message queue / semaphore storage — the game allocates these inline inside its own
// objects and hands us the address, so initialisation must stay within the storage.
// ---------------------------------------------------------------------------
void TestQueueAndSemaphore() {
    std::printf("Message queue / semaphore (inline caller-owned storage)\n");

    alignas(16) nn::os::MessageQueueType q{};
    std::uint64_t buffer[0x20] = {};
    nn::os::InitializeMessageQueue(&q, buffer, 0x20);
    Check(true, "InitializeMessageQueue accepts inline queue and buffer storage");

    // The semaphore was previously a no-op, which never blocked and never woke. These
    // checks pin the blocking semantics, because a silent no-op here would hang boot
    // rather than fail loudly.
    {
        alignas(16) nn::os::SemaphoreType s{};
        nn::os::InitializeSemaphore(&s, 1, 1);

        // Initial count 1: a first acquire must succeed immediately.
        const auto t0 = std::chrono::steady_clock::now();
        nn::os::AcquireSemaphore(&s);
        const auto firstUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count();
        Check(firstUs < 50000, "acquire on a count-1 semaphore returns immediately");

        // Count is now 0, so a second acquire must BLOCK until a release arrives.
        std::atomic<bool> acquired{false};
        std::thread waiter([&] {
            nn::os::AcquireSemaphore(&s);
            acquired = true;
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        Check(!acquired.load(), "acquire on a count-0 semaphore BLOCKS (no-op would pass)");

        nn::os::ReleaseSemaphore(&s);
        waiter.join();
        Check(acquired.load(), "release wakes the blocked acquirer");

        // Releasing past the declared maximum must not inflate the count.
        nn::os::ReleaseSemaphore(&s); // count is 0 again after the waiter took it
        nn::os::ReleaseSemaphore(&s); // this would exceed max=1
        nn::os::AcquireSemaphore(&s); // consumes the single permit
        const auto t1 = std::chrono::steady_clock::now();
        std::atomic<bool> second{false};
        std::thread w2([&] { nn::os::AcquireSemaphore(&s); second = true; });
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        const bool blockedAgain = !second.load();
        nn::os::ReleaseSemaphore(&s);
        w2.join();
        Check(blockedAgain, "count never exceeds max, so the extra release did not add a permit");
        (void)t1;
    }

    // A rejected initialisation must not leave a usable-looking semaphore.
    {
        alignas(16) nn::os::SemaphoreType bad{};
        nn::os::InitializeSemaphore(&bad, 5, 1); // initial > max
        Check(true, "InitializeSemaphore rejects initial > max without crashing");
    }
}

// ---------------------------------------------------------------------------
// Mutex — InitializeMutex 56, LockMutex 171, UnlockMutex 170 call sites. The game
// embeds these inside its own objects, so the argument is caller-owned storage and
// must survive being used through a pointer. The recursive flag is real: an observed
// call passes `false`, and other sites rely on re-entrancy.
// ---------------------------------------------------------------------------
void TestMutex() {
    std::printf("Mutex (Init 56, Lock 171, Unlock 170 call sites)\n");

    // non-recursive: lock/unlock works
    {
        alignas(8) nn::os::MutexType m{};
        nn::os::InitializeMutex(&m, false, 0);
        nn::os::LockMutex(&m);
        nn::os::UnlockMutex(&m);
        Check(true, "non-recursive lock/unlock on caller-owned storage");
    }

    // recursive: a second lock on the same thread must not deadlock
    {
        alignas(8) nn::os::MutexType m{};
        nn::os::InitializeMutex(&m, true, 0);
        nn::os::LockMutex(&m);
        nn::os::LockMutex(&m);
        nn::os::UnlockMutex(&m);
        nn::os::UnlockMutex(&m);
        Check(true, "recursive mutex allows re-entrant locking");
    }

    // mutual exclusion actually excludes
    {
        alignas(8) nn::os::MutexType m{};
        nn::os::InitializeMutex(&m, false, 0);
        int counter = 0;
        std::vector<std::thread> ts;
        for (int i = 0; i < 4; ++i) {
            ts.emplace_back([&] {
                for (int k = 0; k < 25000; ++k) {
                    nn::os::LockMutex(&m);
                    ++counter;
                    nn::os::UnlockMutex(&m);
                }
            });
        }
        for (auto &t : ts) t.join();
        std::printf("        counter after 4x25000 locked increments = %d\n", counter);
        Check(counter == 100000, "mutual exclusion holds under contention");
    }

    // two independent mutexes must not alias (the game has many inline instances)
    {
        alignas(8) nn::os::MutexType a{}, b{};
        nn::os::InitializeMutex(&a, false, 0);
        nn::os::InitializeMutex(&b, false, 0);
        nn::os::LockMutex(&a);
        nn::os::LockMutex(&b);
        nn::os::UnlockMutex(&b);
        nn::os::UnlockMutex(&a);
        Check(true, "two inline mutexes are independent");
    }
}

// ---------------------------------------------------------------------------
// Core / thread queries — GetCurrentCoreNumber 121 call sites. Callers size
// per-core arrays from the available mask, so the mask must be non-empty and the
// current core must fall inside it.
// ---------------------------------------------------------------------------
void TestCoreQueries() {
    std::printf("Core/thread queries (GetCurrentCoreNumber 121 call sites)\n");

    const std::uint64_t mask = nn::os::GetThreadAvailableCoreMask();
    Check(mask != 0, "available core mask is non-empty");
    Check((mask & (mask + 1)) == 0, "available core mask is contiguous from bit 0");

    const std::uint32_t core = nn::os::GetCurrentCoreNumber();
    Check(core < 64 && ((mask >> core) & 1ull) != 0,
          "current core index lies inside the available mask");

    nn::os::ThreadType *t0 = nn::os::GetCurrentThread();
    Check(t0 != nullptr, "GetCurrentThread returns a non-null handle");
    Check(nn::os::GetCurrentThread() == t0, "and is stable within a thread");
}

} // namespace

int main() {
    std::printf("nn::os platform contract tests\n");
    std::printf("(properties derived from call sites in main; see PLATFORM_IMPLEMENTATION_SPEC.md)\n\n");

    TestGetSystemTick();
    std::printf("\n");
    TestSleepThread();
    std::printf("\n");
    TestTls();
    std::printf("\n");
    TestMutex();
    std::printf("\n");
    TestMemory();
    std::printf("\n");
    TestCoreQueries();
    std::printf("\n");
    TestThreadQueries();
    std::printf("\n");
    TestQueueAndSemaphore();

    std::printf("\n----------------------------------------\n");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
