#pragma once
// Lp::Sys::SysInit — the game's startup entry functions.
//
// Evidence (BOOT_CALLGRAPH.md §1-2, ENGINE_REFERENCE.md):
//
//   nninitStartup     0x71011f5ab4  108 bytes  depth 0
//     The very first function executed. Establishes the memory allocator before
//     anything else can run:
//       QueryMemoryInfo → SetMemoryHeapSize → AllocateMemoryBlock → InitializeAllocator
//
//   getCpCallbackSeadAssert  0x71011f3774  12 bytes  depth 1
//     Returns the sead-assert callback pointer consumed by
//     sead::AssertConfig::registerCallback (SysInit::Init step 4).
//     12-byte body: load a function pointer from a data symbol and return it.
//
//   printFromStartTime  0x71011f603c  424 bytes  depth 1
//     Logs an elapsed-time banner to the diagnostic console. Called from
//     parallelInitThreadFunc. Depends on sead::FormatFixedSafeString (not yet ported).
//
// The full SysInit::Init body (0x71011f5b20, 1308 bytes) is not yet reconstructed;
// boot_sequence.cpp covers its 17 observed steps individually.
#include <cstdint>

namespace Lp::Sys {

struct SysInit {
    // 0x71011f5ab4. Runs before SysInit::Init. Allocates the game heap block and
    // installs nn::init's bump allocator. Four nn::os/nn::init calls, in order:
    //   QueryMemoryInfo → SetMemoryHeapSize → AllocateMemoryBlock → InitializeAllocator
    static void nninitStartup();

    // 0x71011f3774. Returns the opaque function pointer that SysInit::Init passes to
    // sead::AssertConfig::registerCallback. The 12-byte body loads one data symbol.
    // Returns nullptr in the port (the real callback walks Lp::Sys internals not yet
    // ported; sead's assert handler is already wired up separately).
    static void *getCpCallbackSeadAssert();

    // 0x71011f603c. Logs a "[label] from start: X.XXX s" banner using the saved start
    // tick from SysInit::Init. Depends on sead::FormatFixedSafeString (not yet ported);
    // the port prints a simpler timestamp.
    static void printFromStartTime(const char *label);
};

} // namespace Lp::Sys
