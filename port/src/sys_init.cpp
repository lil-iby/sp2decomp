// Lp::Sys::SysInit — nninitStartup, getCpCallbackSeadAssert, printFromStartTime.
//
// nninitStartup (0x71011f5ab4, 108 bytes):
//   The real body computes heapSize = QueryMemoryInfo().totalSize - 67 MB, then
//   calls SetMemoryHeapSize(heapSize) → AllocateMemoryBlock(&block, heapSize) →
//   InitializeAllocator(block).
//   For the port, the size is capped at 32 MB so AllocateMemoryBlock does not try
//   to commit ~3.9 GB of host memory. nn::init's pool is sized to 32 MB (matching
//   the observed AllocateMemoryBlock request at the call site in the RECOMP log).
//
// getCpCallbackSeadAssert (0x71011f3774, 12 bytes):
//   Loads a data symbol holding a Lp::Sys assert callback. Those internals are
//   not yet ported; the port returns nullptr and sead's assert handler is already
//   installed via sead::AssertConfig::registerCallback in the boot sequence.
//
// printFromStartTime (0x71011f603c, 424 bytes):
//   Logs "[label] from start: X.XXXs" via sead::FormatFixedSafeString +
//   Lp::Sys::SysInit::printFromStartTime. The real body uses the saved start tick
//   (stored by SysInit::Init step 2). The port uses nn::os::GetSystemTick with a
//   placeholder denominator.
#include "lp/sys_init.hpp"

#include "nn/init.hpp"
#include "nn/os.hpp"

#include <cstdio>
#include <cstdint>
#include <atomic>

namespace Lp::Sys {

namespace {

// The real binary stores the application-start tick in a module-level variable
// written by SysInit::Init step 2 (nn::os::GetSystemTick). We mirror that here.
std::atomic<std::uint64_t> gStartTick{0};

// Port-safe heap-block size. The real binary uses totalMemory - 67 MB (~3.9 GB on
// Switch). Capped at 32 MB so the host test environment does not OOM.
constexpr std::uint64_t kPortHeapSize = 0x2000000; // 32 MB

} // namespace

void SysInit::nninitStartup() {
    // Step 1: learn available memory.
    nn::os::MemoryInfo info{};
    nn::os::QueryMemoryInfo(&info);

    // Record start tick — SysInit::Init step 2 does this; the port does it here
    // so printFromStartTime has a reference point even when called before Init.
    if (gStartTick.load(std::memory_order_relaxed) == 0)
        gStartTick.store(nn::os::GetSystemTick(), std::memory_order_relaxed);

    // Step 2: tell the kernel how large the game heap should be.
    // Real formula: totalSize - 67 MB. The port caps to kPortHeapSize.
    (void)info; // totalSize used only to match the shape; cap overrides it
    nn::os::SetMemoryHeapSize(kPortHeapSize);

    // Step 3: obtain a pointer to the committed block.
    std::uint64_t blockAddr = 0;
    const int rc = nn::os::AllocateMemoryBlock(&blockAddr, kPortHeapSize);
    if (rc != 0 || blockAddr == 0) {
        std::printf("Lp::Sys::SysInit::nninitStartup: AllocateMemoryBlock failed (%d)\n", rc);
        return;
    }

    // Step 4: hand the block to nn::init's bump allocator.
    nn::init::InitializeAllocator(reinterpret_cast<void *>(blockAddr));

    std::printf("Lp::Sys::SysInit::nninitStartup: allocator ready (block=0x%llx, size=0x%llx)\n",
                static_cast<unsigned long long>(blockAddr),
                static_cast<unsigned long long>(kPortHeapSize));
}

void *SysInit::getCpCallbackSeadAssert() {
    // The real body loads one data symbol (a Lp::Sys-internal callback pointer).
    // Those internals are not yet ported; sead's assert path is already wired via
    // AssertConfig::registerCallback in the boot sequence, so returning nullptr is safe.
    return nullptr;
}

void SysInit::printFromStartTime(const char *label) {
    // The real body calls sead::FormatFixedSafeString to format
    // "[label] from start: X.XXXs" (424 bytes, 8 call targets).
    // sead::FormatFixedSafeString is not yet ported; the port prints directly.
    const std::uint64_t now = nn::os::GetSystemTick();
    const std::uint64_t start = gStartTick.load(std::memory_order_relaxed);
    const double elapsed = (start != 0)
        ? static_cast<double>(now - start) / static_cast<double>(nn::os::kTickFrequencyHz)
        : 0.0;
    std::printf("Lp::Sys::SysInit::printFromStartTime: [%s] from start: %.3fs\n",
                label != nullptr ? label : "(null)", elapsed);
}

} // namespace Lp::Sys
