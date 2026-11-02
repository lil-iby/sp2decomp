// sead::HeapMgr — the heap manager, over the host allocator.
//
// Ported from the reference's structure (reference/seadHeapMgr.h) and the boot path's usage.
// The two initialize() overloads and createRootHeap_ are the actual boot entry points;
// everything else here exists to make them work and to be testable.
//
// WHY A HOST ARENA RATHER THAN A REAL ONE: the reference carves root heaps out of an Arena
// with its own layout. The boot path only needs (a) the root heap to exist and (b) it to
// answer getMaxAllocatableSize(8) so the per-manager heaps can be sized. So the "arena" is
// host memory of the requested size, and the root heap is a host-backed ExpHeap over it.
// This is the same interface-not-algorithm choice recorded in HEAP_LAYER_SCOPE.md section 4.
#include "sead/heap_mgr.hpp"

#include <cstdlib>
#include <new>

namespace sead {

namespace {
HeapMgr gInstance;
} // namespace

HeapMgr *HeapMgr::instance() { return &gInstance; }

Arena &HeapMgr::defaultArena() { return gInstance.mDefaultArena; }

Heap **HeapMgr::rootHeaps() { return gInstance.mRootHeaps; }

int HeapMgr::getRootHeapNum() { return gInstance.mRootHeapNum; }

Heap *HeapMgr::getRootHeap(int index) {
    if (index < 0 || index >= gInstance.mRootHeapNum) return nullptr;
    return gInstance.mRootHeaps[index];
}

// `initialize(size)` — the size form. Establishes a host arena of that size and defers to
// the shared body, matching the reference's split between initialize and initializeImpl_.
void HeapMgr::initialize(std::size_t size) {
    if (size == 0) return;
    void *mem = std::malloc(size);
    if (mem == nullptr) return;
    gInstance.mDefaultArena = Arena(mem, size);
    initializeImpl_();
}

// `initialize(Arena*)` — the arena form. The caller supplies the region.
void HeapMgr::initialize(Arena *arena) {
    if (arena == nullptr || !arena->isInitialized()) return;
    gInstance.mDefaultArena = *arena;
    initializeImpl_();
}

// Shared body: create the root heap from the arena. The reference does more here (hostio
// init, cache setup, locks); none of it is reachable on the boot path, so this creates the
// root heap and stops, rather than inventing behaviour.
void HeapMgr::initializeImpl_() { createRootHeap_(); }

// Creates the root heap. The boot path then reads it as sRootHeaps[0] and sizes every
// system-manager heap from it via getMaxAllocatableSize(8).
void HeapMgr::createRootHeap_() {
    Arena &arena = gInstance.mDefaultArena;
    if (!arena.isInitialized()) return;
    if (gInstance.mRootHeapNum >= kMaxRootHeaps) return;

    // The root heap is a host-backed ExpHeap whose declared budget is the arena size, so
    // getMaxAllocatableSize(8) answers meaningfully for the per-manager heaps.
    ExpHeap *root = ExpHeap::create(arena.size(), "sead::RootHeap");
    if (root == nullptr) return;
    gInstance.mRootHeaps[gInstance.mRootHeapNum++] = root;
    // The first root heap becomes current, which is what the reference's initializeImpl_
    // arranges before returning.
    gInstance.mCurrentHeap = root;
}

Heap *HeapMgr::findContainHeap(const void *ptr) {
    if (ptr == nullptr) return nullptr;
    const int n = gInstance.mRootHeapNum;
    for (int i = 0; i < n; ++i) {
        Heap *h = gInstance.mRootHeaps[i];
        if (h && h->isInclude(ptr)) return h;
    }
    return nullptr;
}

void HeapMgr::destroy() {
    for (int i = 0; i < gInstance.mRootHeapNum; ++i) {
        if (gInstance.mRootHeaps[i] != nullptr) {
            gInstance.mRootHeaps[i]->destroy();
            delete gInstance.mRootHeaps[i];
            gInstance.mRootHeaps[i] = nullptr;
        }
    }
    gInstance.mRootHeapNum = 0;
    gInstance.mCurrentHeap = nullptr;
    // NOTE: the arena memory itself is intentionally not freed here. The size form
    // allocated it, but the arena form is caller-owned, and the reference does not free
    // caller arenas either. Not freeing a boot-lifetime allocation matches the original.
}

// Returns the PREVIOUS current heap, or the sentinel 1 when there was none -- the binary's
// caller tests `if (prev != 1)` before restoring.
Heap *HeapMgr::setCurrentHeap_(Heap *heap) {
    Heap *previous = gInstance.mCurrentHeap;
    gInstance.mCurrentHeap = heap;
    if (previous == nullptr) return reinterpret_cast<Heap *>(1);
    return previous;
}

Heap *HeapMgr::getCurrentHeap() const {
    return gInstance.mCurrentHeap;
}

} // namespace sead
