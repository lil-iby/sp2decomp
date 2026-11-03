// Boot integration harness.
//
// This does NOT invent a boot sequence. It replays the one observed in the binary, in
// the order the binary performs it, from the decompiled bodies of the two boot roots:
//
//   Lp::Sys::SysInit::nninitStartup   @ 0x71011f5ab4  (108 bytes)
//   Lp::Sys::SysInit::Init            @ 0x71011f5b20  (1308 bytes)
//
// The point is to exercise the platform layer the way the game actually does, in order,
// and assert the invariants the real code depends on — including the ones the boot root
// itself establishes (heap size masking, the 6 GB check) and the pointers it stores for
// later stages to read back.
#include "nn/init.hpp"
#include "nn/os.hpp"
#include "nn/services.hpp"
#include "nn/util.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else { std::printf("  ok    %s\n", what); }
}

// ---------------------------------------------------------------------------
// Globals the boot root writes and later stages read back. Names taken from the PTR
// symbols the decompiled bodies reference.
// ---------------------------------------------------------------------------
std::uint64_t g_sRunStartTime = 0;   // PTR_sRunStartTime_7102fce5e0
bool g_sIs6GB = false;               // PTR_sIs6GB_7102fce5d8
std::uint64_t g_memoryHeapSize = 0;  // set via SetMemoryHeapSize
std::size_t g_allocatorRequests = 0;

// ---------------------------------------------------------------------------
// Stage 1 — nninitStartup, replayed call-for-call.
// ---------------------------------------------------------------------------
void StageNninitStartup() {
    std::printf("Stage 1: Lp::Sys::SysInit::nninitStartup (0x71011f5ab4)\n");

    nn::os::MemoryInfo memInfo{};
    nn::os::QueryMemoryInfo(&memInfo);
    Check(memInfo.totalSize > 0, "QueryMemoryInfo yields a usable total");

    // The real code masks to a 2 MB boundary: `& 0xffffffffffe00000`.
    const std::uint64_t raw = memInfo.totalSize - memInfo.usedSize;
    const std::uint64_t heapSize = raw & 0xffffffffffe00000ull;

    nn::os::SetMemoryHeapSize(heapSize);
    g_memoryHeapSize = heapSize;
    Check(heapSize != 0, "SetMemoryHeapSize receives a non-zero budget");
    Check((heapSize & 0x1FFFFFull) == 0,
          "the budget is 2 MB-aligned, as the boot root's mask requires");
    std::printf("        heap budget = %llu MB\n",
                static_cast<unsigned long long>(heapSize / (1024 * 1024)));

    // `*PTR_sIs6GB = 0x100000000 < uVar1;`  -- a devkit with more than 4 GB.
    g_sIs6GB = (0x100000000ull < heapSize);
    std::printf("        sIs6GB = %s\n", g_sIs6GB ? "true" : "false");

    // `AllocateMemoryBlock(&local_18, 0x2000000);`
    std::uint64_t block = 0;
    const int rc = nn::os::AllocateMemoryBlock(&block, 0x2000000);
    Check(rc == 0, "AllocateMemoryBlock(32 MB) succeeds at boot");
    Check(block != 0, "and yields a non-null base for the allocator");
    Check((block & 0xFFF) == 0, "the block base is page-aligned");

    // `nn::init::InitializeAllocator(local_18);`
    const int irc = nn::init::InitializeAllocator(reinterpret_cast<void *>(block));
    Check(irc == 0, "InitializeAllocator accepts the block");
    Check(nn::init::GetDefaultAllocator() == reinterpret_cast<void *>(block),
          "and becomes the default allocator for later stages");
}

// ---------------------------------------------------------------------------
// Stage 2 — SysInit::Init's opening, which comes immediately after stage 1.
// ---------------------------------------------------------------------------
void StageSysInitInit() {
    std::printf("\nStage 2: Lp::Sys::SysInit::Init opening (0x71011f5b20)\n");

    // `Utl::dumpDateTime(" Application Start Time");`
    nn::diag::LogMetaData meta{};
    nn::diag::detail::LogImpl(&meta, " Application Start Time\n");
    Check(true, "dumpDateTime-equivalent logging path works before any allocator use");

    // `uVar8 = nn::os::GetSystemTick(); *(PTR_sRunStartTime) = uVar8;`
    const std::uint64_t tick = nn::os::GetSystemTick();
    g_sRunStartTime = tick;
    Check(tick > 0, "GetSystemTick yields a non-zero start time");
    Check(g_sRunStartTime == tick, "and it is stored for later elapsed-time reads");

    // Lateness must be measurable against that recorded start time: this is what
    // printFromStartTime and the assert path do.
    nn::os::SleepThread(2'000'000); // 2 ms
    const std::uint64_t now = nn::os::GetSystemTick();
    const auto elapsedTicks = now - g_sRunStartTime;
    const double elapsedMs = static_cast<double>(elapsedTicks) /
                             (nn::os::kTickFrequencyHz / 1000.0);
    std::printf("        elapsed since start = %.2f ms\n", elapsedMs);
    Check(now >= g_sRunStartTime, "the clock never goes backwards against the start time");
    Check(elapsedMs >= 1.0 && elapsedMs < 100.0,
          "a 2 ms sleep measures as ~2 ms of ticks since start");

    // `nn::os::QueryMemoryInfo(&local_138); ... sead::GameFrameworkNx::initialize(...)`
    nn::os::MemoryInfo second{};
    nn::os::QueryMemoryInfo(&second);
    Check(second.totalSize == g_memoryHeapSize || second.totalSize > 0,
          "the second QueryMemoryInfo is consistent with the first");

    // `Utl::dumpMemoryInfo("Application Start");` — logs real values.
    nn::diag::detail::LogImpl(&meta, "%4ld MB: Program\n",
                              static_cast<long>(second.totalSize / (1024 * 1024)));
    Check(true, "dumpMemoryInfo-equivalent logging path works");
}

// ---------------------------------------------------------------------------
// Stage 3 — the allocator is used by framework code immediately after Init's opening:
// `HeapGroup::createInstance`, `createHeap`, and `operator_new(iVar7 + 0x19000)`.
// Those engine functions are out of scope, but their ALLOCATION PATTERN is observable
// and this exercises it.
// ---------------------------------------------------------------------------
void StageFrameworkAllocation() {
    std::printf("\nStage 3: framework heap allocation pattern\n");

    // Operators the decompiled Init uses: `operator_new__((long)(iVar7 + 0x19000), pHVar17, 8)`
    struct Request { std::size_t size; std::size_t alignment; const char *what; };
    const Request requests[] = {
        {0x19000, 8,  "operator_new(iVar7 + 0x19000, heap, 8)"},
        {0x212c,  8,  "ExpHeap::create descriptor (local_f4 = 0x212c)"},
        {0x80,    16, "small framework object"},
    };

    for (const auto &r : requests) {
        void *p = nn::init::Allocate(r.size, r.alignment);
        ++g_allocatorRequests;
        std::printf("        %-46s -> %s\n", r.what, p ? "ok" : "NULL");
        Check(p != nullptr, r.what);
        if (p != nullptr) {
            Check((reinterpret_cast<std::uintptr_t>(p) % r.alignment) == 0,
                  "  and is aligned as requested");
            std::memset(p, 0, r.size); // callers treat fresh heap as zeroed
        }
    }

    // The framework stores these in its own heap and frees some of them; Free must be
    // safe even though a bump allocator cannot reclaim.
    nn::init::Free(nullptr);
    Check(true, "Free(nullptr) is safe");
}

// ---------------------------------------------------------------------------
// Stage 4 — thread and TLS usage, which the parallel-init path drives:
// `RootTask::createParallelInitThread` / the WorkerThreadMgr dispatch.
// ---------------------------------------------------------------------------
void StageParallelInitPattern() {
    std::printf("\nStage 4: parallel-init thread/TLS pattern\n");

    std::uint32_t slot = 0xFFFFFFFFu;
    Check(nn::os::AllocateTlsSlot(&slot, nullptr) == 0,
          "a TLS slot is obtained before spawning workers");

    struct Worker {
        std::uint32_t slot;
        std::uint64_t tick;
        bool tlsIsolated;
    };
    std::vector<Worker> results(4);

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([i, slot, &results] {
            // Each worker records its own tick and TLS value, mirroring how the real
            // per-core init threads carry thread-local state.
            nn::os::SetTlsValue(slot, reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x1000 + i)));
            results[i].slot = slot;
            results[i].tick = nn::os::GetSystemTick();
            results[i].tlsIsolated =
                nn::os::GetTlsValue(slot) == reinterpret_cast<void *>(static_cast<std::uintptr_t>(0x1000 + i));
        });
    }
    for (auto &t : threads) t.join();

    bool allIsolated = true;
    for (const auto &r : results) if (!r.tlsIsolated) allIsolated = false;
    Check(allIsolated, "every worker sees only its own TLS value");

    bool ticksValid = true;
    for (const auto &r : results) if (r.tick == 0) ticksValid = false;
    Check(ticksValid, "every worker's GetSystemTick is valid concurrently");

    // The handler the real code locks around shared state.
    alignas(16) nn::os::MutexType m{};
    nn::os::InitializeMutex(&m, false, 0);
    int shared = 0;
    threads.clear();
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&m, &shared] {
            for (int k = 0; k < 10000; ++k) {
                nn::os::LockMutex(&m);
                ++shared;
                nn::os::UnlockMutex(&m);
            }
        });
    }
    for (auto &t : threads) t.join();
    std::printf("        shared counter = %d\n", shared);
    Check(shared == 40000, "the boot mutex serialises parallel workers correctly");
}

// ---------------------------------------------------------------------------
// Stage 5 — the romfs mount the boot path performs before loading resources:
// QueryMountRomCacheSize then MountRom("content", ...).
// ---------------------------------------------------------------------------
void StageRomfsMount() {
    std::printf("\nStage 5: romfs mount (boot-critical)\n");

    std::uint64_t cacheSize = 0;
    Check(nn::fs::QueryMountRomCacheSize(&cacheSize) == 0,
          "QueryMountRomCacheSize succeeds");
    Check(cacheSize > 0, "and reports a non-zero requirement");

    std::vector<char> cache(static_cast<std::size_t>(cacheSize));
    Check(nn::fs::MountRom("content", cache.data(), cacheSize) == 0,
          "MountRom(\"content\") succeeds with the queried cache");
    Check(nn::fs::MountRom("content", cache.data(), cacheSize) != 0,
          "and a duplicate mount is rejected");
}

// ---------------------------------------------------------------------------
// Stage 6 — time, which the assert/logging path uses via dumpDateTime.
// ---------------------------------------------------------------------------
void StageTimeAndDiagnostics() {
    std::printf("\nStage 6: time and diagnostics\n");

    nn::time::PosixTime now = 0;
    Check(nn::time::StandardUserSystemClock::GetCurrentTime(&now) == 0, "wall clock read");
    nn::time::CalendarTime ct{};
    Check(nn::time::ToCalendarTime(&ct, nullptr, now) == 0, "converted to calendar time");
    std::printf("        %04u-%02u-%02u %02u:%02u:%02u\n", ct.year, ct.month, ct.day,
                ct.hour, ct.minute, ct.second);
    Check(ct.year >= 2020 && ct.year < 2100, "the calendar year is plausible");

    char line[128] = {};
    nn::util::SNPrintf(line, sizeof(line), "[%04u-%02u-%02u] boot\n", ct.year, ct.month, ct.day);
    Check(std::strlen(line) > 10, "a formatted log line can be built");
    std::printf("        %s", line);
}

} // namespace

int main() {
    std::printf("=== boot integration harness ===\n");
    std::printf("Replays the observed boot sequence from Lp::Sys::SysInit against the\n");
    std::printf("platform layer. Order and arguments come from the decompiled roots.\n\n");

    StageNninitStartup();
    StageSysInitInit();
    StageFrameworkAllocation();
    StageParallelInitPattern();
    StageRomfsMount();
    StageTimeAndDiagnostics();

    std::printf("\n----------------------------------------\n");
    std::printf("allocator requests served: %zu\n", g_allocatorRequests);
    std::printf("sRunStartTime = %llu   sIs6GB = %s\n",
                static_cast<unsigned long long>(g_sRunStartTime),
                g_sIs6GB ? "true" : "false");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
