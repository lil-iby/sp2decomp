// Contract tests for Lp::Sys::HeapGroup and sead::ForegroundBucketMgr.
//
// HeapGroup::createInstance (0x71011f4ffc, 160 bytes), HeapGroup::createHeap
// (0x71011f5130, 508 bytes), and ForegroundBucketMgr::initialize (0x710139fb3c, 4 bytes --
// a genuine no-op in the binary).
#include "lp/heap_group.hpp"
#include "sead/heap.hpp"
#include "sead/heap_mgr.hpp"
#include "sead/framework_args.hpp"

#include <cstdio>
#include <string>

namespace {
int g_failures = 0;
int g_checks = 0;
void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else { std::printf("  ok    %s\n", what); }
}
} // namespace

int main() {
    std::printf("Lp::Sys::HeapGroup / sead::ForegroundBucketMgr contract tests\n\n");

    sead::HeapMgr::destroy();
    sead::Framework::InitializeArg arg;
    sead::Framework::initialize(arg);
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    Check(root != nullptr, "a root heap exists to allocate the singleton from");

    std::printf("\nHeapGroup::createInstance (0x71011f4ffc)\n");
    Check(Lp::Sys::HeapGroup::instance() == nullptr,
          "no singleton exists before createInstance");

    const int liveBefore = root->liveAllocationCount();
    Lp::Sys::HeapGroup *hg = Lp::Sys::HeapGroup::createInstance(root);
    Check(hg != nullptr, "createInstance returns the singleton");
    Check(Lp::Sys::HeapGroup::instance() == hg, "and instance() agrees");
    Check(root->liveAllocationCount() == liveBefore + 1,
          "exactly one allocation was made from the supplied heap");

    // Idempotence: `if (plVar4 == 0) { ... } return plVar4;`
    Check(Lp::Sys::HeapGroup::createInstance(root) == hg,
          "a second createInstance returns the SAME singleton");
    Check(root->liveAllocationCount() == liveBefore + 1,
          "and allocates nothing further (idempotent)");

    hg->criticalSection().lock();
    hg->criticalSection().unlock();
    Check(true, "the CriticalSection locks and unlocks");

    hg->readWriteLock().writeLock();
    hg->readWriteLock().writeUnlock();
    hg->readWriteLock().readLock();
    hg->readWriteLock().readUnlock();
    Check(true, "the ReadWriteLock takes both read and write");

    std::printf("\nHeapGroup::createHeap (0x71011f5130)\n");
    {
        Lp::Sys::HeapSizes sizes;
        for (int i = 0; i < 5; ++i) sizes.v[i] = 0x10000u * static_cast<unsigned>(i + 1);
        const char *names[5] = {"cGroup0", "cGroup1", "cGroup2", "cGroup3", "cGroup4"};

        hg->createHeap(root, sizes, names, "cOthers");

        int nonNull = 0;
        for (int i = 0; i < Lp::Sys::HeapGroup::kHeapCount; ++i) {
            if (hg->heapAt(i) != nullptr) ++nonNull;
        }
        Check(nonNull == Lp::Sys::HeapGroup::kHeapCount,
              "all six slots hold a heap after createHeap");

        Check(std::string(hg->heapAt(0)->name()) == "cGroup0",
              "slot 0 carries Group::text_(0)");
        Check(std::string(hg->heapAt(4)->name()) == "cGroup4",
              "slot 4 carries Group::text_(4)");
        Check(std::string(hg->heapAt(5)->name()) == "cOthers",
              "the SIXTH slot uses the literal \"cOthers\", not an indexed name");

        // The decompiled body gives the sixth heap a LITERAL SIZE OF 0, unlike the five that
        // take their size from the info table. Asserted so it cannot be "tidied" away.
        Check(hg->heapAt(5)->getSize() == 0,
              "and the sixth heap has size 0, exactly as the body passes");
        Check(hg->heapAt(0)->getSize() == 0x10000,
              "while slot 0 takes its size from the info table");
        Check(hg->heapAt(4)->getSize() == 0x50000,
              "and slot 4 takes info[4]");

        // +0x150 is ZEROED AND NEVER ASSIGNED. Reproduced, not fixed.
        sead::Heap **slot150 = reinterpret_cast<sead::Heap **>(
            reinterpret_cast<unsigned char *>(hg) + 0x150);
        Check(*slot150 == nullptr,
              "+0x150 is zeroed and left null, as the decompiled body does");

        // createHeap zeroes its slots first, so it must be re-runnable.
        hg->createHeap(root, sizes, names, "cOthers");
        Check(hg->heapAt(0) != nullptr && hg->heapAt(5) != nullptr,
              "createHeap is re-runnable (it zeroes its slots first)");
    }

    std::printf("\nForegroundBucketMgr::initialize (0x710139fb3c, 4-byte no-op)\n");
    {
        const int before = root->liveAllocationCount();
        sead::ForegroundBucketMgr::initialize(root, 1u, 2u, true);
        Check(true, "the no-op runs without effect, as the binary's body does");
        Check(root->liveAllocationCount() == before, "and allocates nothing");
        sead::ForegroundBucketMgr::initialize(nullptr, 0u, 0u, false);
        Check(true, "and is safe with null/invalid arguments too");
    }

    std::printf("\nrefusal paths\n");
    Lp::Sys::HeapGroup::deleteInstance();
    Check(Lp::Sys::HeapGroup::instance() == nullptr, "deleteInstance clears the singleton");
    Check(Lp::Sys::HeapGroup::createInstance(nullptr) == nullptr,
          "createInstance(nullptr) refuses rather than allocating elsewhere");
    Check(Lp::Sys::HeapGroup::instance() == nullptr,
          "and leaves no half-built singleton behind");

    std::printf("\n----------------------------------------\n");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
