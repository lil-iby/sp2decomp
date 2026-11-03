// sead::GameFramework — ported from the decompiled body at 0x7100e69fe4 (456 bytes).
//
// The body is ONE PATTERN APPLIED PER SYSTEM MANAGER, not 456 bytes of unique logic:
//
//   void sead::GameFramework::initialize(const Framework::InitializeArg& arg) {
//       Framework::initialize(arg);                                  // 0x7100e698ac
//       Heap* root = <sRootHeaps[0]>;
//       size_t sz = root->getMaxAllocatableSize(root, 8);            // vptr+0x80
//
//       Heap* sys = ExpHeap::create(sz, {"sead::SystemManagers"}, root, 8, 1, 0);
//       sz = sys->getMaxAllocatableSize(sys, 8);
//       Heap* res = ExpHeap::create(sz, {"sead::ResourceMgr"}, sys, 8, 1, 0);
//       if (res) prev = HeapMgr::setCurrentHeap_(res);
//       ResourceMgr::createInstance(res);
//       res->adjust(res);                                            // vptr+0x28
//       if (prev != 1) HeapMgr::setCurrentHeap_(prev);
//
//       sz = sys->getMaxAllocatableSize(sys, 8);
//       Heap* fdev = ExpHeap::create(sz, {"sead::FileDeviceMgr"}, sys, 8, 1, 0);
//       if (fdev) prev2 = HeapMgr::setCurrentHeap_(fdev);
//       FileDeviceMgr::createInstance(fdev);
//       fdev->adjust(fdev);
//       if (prev2 != 1) HeapMgr::setCurrentHeap_(prev2);
//       ... further managers follow the same shape ...
//   }
//
// The managers are created under the SystemManagers heap (a child of the root heap), each
// installed as current only for its own createInstance call and then restored -- so a manager
// that allocates during construction lands in its own heap.
#pragma once

#include "sead/file_device.hpp" // sead::FileDeviceMgr, the second manager created here
#include "sead/framework_args.hpp"

namespace sead {

class Heap;

// sead::ResourceMgr — created with its own heap during GameFramework::initialize.
class ResourceMgr {
public:
    static ResourceMgr *createInstance(Heap *heap);
    static ResourceMgr *instance();
    Heap *heap() const { return mHeap; }

private:
    Heap *mHeap = nullptr;
};

class GameFramework : public Framework {
public:
    // 0x7100e69fe4, 456 bytes. The tree root of the boot step the runner currently stops at.
    static void initialize(const InitializeArg &arg);

    // The SystemManagers heap, which every per-manager heap is a child of. Retained so the
    // sequence can be inspected and a later stage can find it, as the binary does via
    // PTR_sRootHeaps.
    static Heap *systemManagersHeap();
};

} // namespace sead
