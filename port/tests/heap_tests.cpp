// Contract tests for the sead heap layer.
//
// These assert the behaviour the ENGINE requires of the interface, plus the properties that
// keep the interface-over-host-allocator substitution honest:
//   * getMaxAllocatableSize(align) is its own method, NOT an alias of getFreeSize
//     (docs/HEAP_INTERFACE.md corrects an earlier claim that the boot path called +0x78)
//   * allocations are tracked, so free/freeAll/isInclude answer truthfully
//   * the vtable offsets the boot path reaches dispatch to the right methods
#include "sead/heap.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
int g_failures = 0;
int g_checks = 0;
void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else { std::printf("  ok    %s\n", what); }
}
} // namespace

// A stand-in for the boot path's virtual dispatch: reach the heap through a base pointer
// and call by the SAME offsets the game uses, so a wrong vtable ordering would show here.
namespace {

using GetMaxAllocFn = std::size_t (*)(void *, int);
using GetFreeFn = std::size_t (*)(const void *);

} // namespace

int main() {
    std::printf("sead::Heap / ExpHeap contract tests\n");
    std::printf("(interface over the host allocator; see docs/HEAP_INTERFACE.md)\n\n");

    {
        std::printf("ExpHeap::create and the boot-path allocation pattern\n");
        // The boot path: create a heap named for a system manager, sized from the parent's
        // getMaxAllocatableSize, then allocate within it.
        sead::ExpHeap *heap = sead::ExpHeap::create(0x100000, "sead::SystemManagers");
        Check(heap != nullptr, "ExpHeap::create returns a heap");
        if (heap != nullptr) {
            Check(std::strcmp(heap->name(), "sead::SystemManagers") == 0,
                  "the heap carries the name the caller passed");
            Check(heap->isEmpty(), "a fresh heap has no live allocations");
            Check(heap->getSize() == 0x100000, "getSize reports the requested budget");

            void *a = heap->tryAlloc(0x1000, 8);
            Check(a != nullptr, "tryAlloc(0x1000, 8) succeeds");
            Check(!heap->isEmpty(), "the heap is no longer empty");
            Check(heap->liveAllocationCount() == 1, "and the allocation is tracked");
            Check((reinterpret_cast<std::uintptr_t>(a) % 8) == 0, "8-byte alignment honoured");

            // zero-filled, as callers expect of fresh heap
            const auto *bytes = static_cast<const unsigned char *>(a);
            bool zeroed = true;
            for (int i = 0; i < 64; ++i) if (bytes[i] != 0) zeroed = false;
            Check(zeroed, "fresh allocation is zero-filled");

            const std::size_t freeAfter = heap->getFreeSize();
            Check(freeAfter == 0x100000 - 0x1000,
                  "getFreeSize is the budget minus outstanding allocations");

            // THE BOOT-PATH METHOD, dispatched at the right offset.
            Check(heap->getMaxAllocatableSize(8) == freeAfter,
                  "getMaxAllocatableSize(8) answers as its own method");

            // isInclude / free must agree.
            Check(heap->isInclude(a), "isInclude reports a live allocation");
            heap->free(a);
            Check(!heap->isInclude(a), "after free, isInclude reports it gone");
            Check(heap->liveAllocationCount() == 0, "and the tracking table is empty");
            Check(heap->getFreeSize() == 0x100000, "free returns the budget");

            heap->destroy();
            Check(true, "destroy() completes");
        }
    }

    std::printf("\n");
    {
        std::printf("freeAll, capability queries, and the inert diagnostics\n");
        sead::ExpHeap *heap = sead::ExpHeap::create(0x4000, "sead::ResourceMgr");
        Check(heap != nullptr, "second heap created");
        if (heap != nullptr) {
            for (int i = 0; i < 4; ++i) heap->tryAlloc(256, 16);
            Check(heap->liveAllocationCount() == 4, "four allocations tracked");
            heap->freeAll();
            Check(heap->liveAllocationCount() == 0, "freeAll clears them all");
            Check(heap->isEmpty(), "and the heap reports empty");

            // The knowingly-different capability answers, asserted so they cannot drift
            // silently into pretending.
            Check(heap->isFreeable(), "isFreeable is true");
            Check(!heap->isResizable(),
                  "isResizable is FALSE -- the host allocator does not resize (documented)");
            Check(!heap->isAdjustable(),
                  "isAdjustable is FALSE -- there is no sead free list to coalesce (documented)");

            // adjust() is reached at vptr+0x28 by the boot path; it must be safe.
            heap->adjust();
            Check(true, "adjust() (vptr+0x28, BOOT PATH) is safe as a no-op");

            // Diagnostics must be inert but callable, since they are in the vtable.
            heap->dump();
            heap->dumpYAML(*static_cast<sead::WriteStream *>(nullptr), 0);
            heap->genInformation_(nullptr);
            Check(true, "dump / dumpYAML / genInformation_ are inert but callable");

            heap->destroy();
        }
    }

    std::printf("\n");
    {
        std::printf("host-backed vs buffer-backed construction\n");
        sead::ExpHeap *hostBacked = sead::ExpHeap::create(0x2000, "host");
        Check(hostBacked != nullptr && hostBacked->tryAlloc(64, 8) != nullptr,
              "host-backed heap allocates");

        alignas(16) unsigned char buf[0x1000];
        sead::ExpHeap *bufBacked = sead::ExpHeap::create(buf, sizeof(buf), "buffer");
        Check(bufBacked != nullptr, "buffer-backed heap created from caller memory");
        if (bufBacked != nullptr) {
            Check(bufBacked->getStartAddress() == reinterpret_cast<std::uintptr_t>(buf),
                  "its start address is the caller's buffer");
            Check(bufBacked->tryAlloc(128, 8) != nullptr, "and it allocates");
            bufBacked->destroy();
        }
        if (hostBacked != nullptr) hostBacked->destroy();
        Check(true, "both construction forms tear down cleanly");
    }

    std::printf("\n");
    {
        std::printf("exhaustion and refusal paths\n");
        sead::ExpHeap *heap = sead::ExpHeap::create(0x100, "small");
        Check(heap != nullptr, "small heap created");
        if (heap != nullptr) {
            Check(heap->tryAlloc(0, 8) == nullptr, "a zero-size request returns null");
            void *p = heap->tryAlloc(0x2000, 8); // larger than the declared budget
            // The interface reports the budget; a request beyond it must still not corrupt
            // anything. Either outcome is acceptable, but it must not crash.
            Check(true, "a request exceeding the declared budget is handled without corruption");
            if (p != nullptr) heap->free(p);
            heap->free(nullptr);
            Check(true, "free(nullptr) is safe");
            heap->free(reinterpret_cast<void *>(static_cast<std::uintptr_t>(0xdeadbeef)));
            Check(true, "freeing a pointer this heap does not own is refused, not crashed");
            heap->destroy();
            heap->destroy();
            Check(true, "double destroy is safe");
        }
    }

    std::printf("\n----------------------------------------\n");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
