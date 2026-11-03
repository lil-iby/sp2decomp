// sead::GameFramework and its two named system managers.
#include "sead/game_framework.hpp"

#include "sead/heap.hpp"
#include "sead/heap_mgr.hpp"

namespace sead {

namespace {

Heap *gSystemManagersHeap = nullptr;

ResourceMgr gResourceMgr;

// One iteration of the pattern the decompiled body repeats. Kept as a helper so the shared
// shape is visible in one place rather than transcribed four times -- the four transcriptions
// in the binary differ only in the manager and the heap name.
//
//   sz  = parent->getMaxAllocatableSize(parent, 8);      // vptr+0x80
//   h   = ExpHeap::create(sz, name, parent, 8, 1, 0);
//   prev = h ? HeapMgr::setCurrentHeap_(h) : 1;
//   <create the manager with h>
//   h->destroy(h);                                       // vptr+0x28
//   if (prev != 1) HeapMgr::setCurrentHeap_(prev);
Heap *CreateManagerHeap(Heap *parent, const char *name) {
    if (parent == nullptr) return nullptr;
    const std::size_t sz = parent->getMaxAllocatableSize(8);
    if (sz == 0) return nullptr;
    return ExpHeap::create(sz, name, parent, 8, HeapDirection::eForward, true);
}

} // namespace

Heap *GameFramework::systemManagersHeap() { return gSystemManagersHeap; }

void GameFramework::initialize(const InitializeArg &arg) {
    // Clear any heap recorded by a previous call, so a refused run cannot leave stale state
    // (the same defect the Framework::initialize test caught).
    gSystemManagersHeap = nullptr;

    // --- Framework::initialize(arg) -- the base class runs first ------------------------
    Framework::initialize(arg);

    Heap *root = HeapMgr::getRootHeap(0);
    if (root == nullptr) return; // initialize refused

    // --- sead::SystemManagers: a child heap of the root --------------------------------
    //   pHVar5 = (Heap *)ExpHeap::create(lVar3, (long)&local_40, pHVar5, 8, 1, 0);
    //   local_38 = "sead::SystemManagers";
    Heap *sys = CreateManagerHeap(root, "sead::SystemManagers");
    if (sys == nullptr) return;
    gSystemManagersHeap = sys;

    // --- sead::ResourceMgr, under the SystemManagers heap ------------------------------
    {
        Heap *res = CreateManagerHeap(sys, "sead::ResourceMgr");
        if (res != nullptr) {
            Heap *prev = HeapMgr::instance()->setCurrentHeap_(res);
            ResourceMgr::createInstance(res);
            // Slot +0x28 is adjust (shrink to what was used), not destroy (+0x20): destroy
            // would free the manager's allocations right after creating them.
            res->adjust();
            if (prev != reinterpret_cast<Heap *>(1)) HeapMgr::instance()->setCurrentHeap_(prev);
        }
    }

    // --- sead::FileDeviceMgr, same shape -----------------------------------------------
    {
        Heap *fdev = CreateManagerHeap(sys, "sead::FileDeviceMgr");
        if (fdev != nullptr) {
            Heap *prev = HeapMgr::instance()->setCurrentHeap_(fdev);
            FileDeviceMgr::createInstance(fdev); // mounts the RomFS as "content"
            fdev->adjust();
            if (prev != reinterpret_cast<Heap *>(1)) HeapMgr::instance()->setCurrentHeap_(prev);
        }
    }

    // The remaining managers in the binary follow this identical shape. They are NOT
    // transcribed here because their names were not all recovered from the 456-byte body --
    // inventing manager names would be guessing. Recorded as a limit rather than filled in.
}

// --- the two managers -------------------------------------------------------------------
ResourceMgr *ResourceMgr::createInstance(Heap *heap) {
    gResourceMgr.mHeap = heap;
    return &gResourceMgr;
}
ResourceMgr *ResourceMgr::instance() { return &gResourceMgr; }

} // namespace sead
