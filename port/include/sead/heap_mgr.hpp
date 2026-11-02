// sead::HeapMgr — ported from the open-ead/sead reference and the binary's symbols.
//
// Reference (reference/seadHeapMgr.h):
//   static void initialize(size_t size);        @ 0x7100e7c4e0 (pead) / boot path
//   static void initializeImpl_();              @ 0x7100e7c5e4
//   static void initialize(Arena* arena);       @ 0x7100e7c6b8
//   static void createRootHeap_();              @ 0x7100e7c790
//   static HeapMgr* instance();
//   static Heap* getRootHeap(s32 index);
//   Heap* setCurrentHeap_(Heap* heap);          @ 0x7100e7cfc0
//   static HeapMgr* sInstancePtr;
//   using RootHeaps = FixedPtrArray<Heap, 4>;
//   static RootHeaps sRootHeaps;
//   static Arena sDefaultArena;
//
// The boot path in Framework::initialize does:
//   if (arg.arena == nullptr) HeapMgr::initialize(arg.heap_size);
//   else                      HeapMgr::initialize(arg.arena);
//   ...
//   Heap* root = sRootHeaps[0];        // via PTR_sRootHeaps_7102fc9f98
// and GameFramework::initialize does the same, then sizes each system-manager heap with
// root->getMaxAllocatableSize(8).
#pragma once

#include "sead/heap.hpp"

#include <cstddef>
#include <cstdint>

namespace sead {

// sead::Arena — a memory region the heap manager carves root heaps from. Only the fact that
// it can be passed to initialize() and that it exposes an address and size is needed here;
// the reference's full Arena is not modelled because nothing on the boot path reads its
// internals.
class Arena {
public:
    Arena() = default;
    Arena(void *address, std::size_t size) : mAddress(address), mSize(size) {}

    void *address() const { return mAddress; }
    std::size_t size() const { return mSize; }

    bool isInitialized() const { return mAddress != nullptr && mSize != 0; }

private:
    void *mAddress = nullptr;
    std::size_t mSize = 0;
};

class HeapMgr {
public:
    // Maximum number of root heaps, per the reference's FixedPtrArray<Heap, 4>.
    static constexpr int kMaxRootHeaps = 4;

    // Boot entry, size form: `if (arg.arena == nullptr) HeapMgr::initialize(arg.heap_size);`
    static void initialize(std::size_t size);
    // Boot entry, arena form: `else HeapMgr::initialize(arg.arena);`
    static void initialize(Arena *arena);
    // Shared body both forms funnel into.
    static void initializeImpl_();
    // Creates the root heap inside the current arena.
    static void createRootHeap_();

    static void destroy();

    static HeapMgr *instance();

    static int getRootHeapNum();
    static Heap *getRootHeap(int index);

    // Installs `heap` as the current heap and returns the PREVIOUS one, which callers
    // restore later. The binary's GameFramework::initialize does:
    //   prev = HeapMgr::setCurrentHeap_(sInstancePtr, newHeap);
    //   ... build ...
    //   if (prev != 1) HeapMgr::setCurrentHeap_(sInstancePtr, prev);
    // The sentinel value 1 means "there was no previous heap".
    Heap *setCurrentHeap_(Heap *heap);
    Heap *getCurrentHeap() const;

    // The root heaps, backing PTR_sRootHeaps_7102fc9f98. The boot path reads index 0.
    static Heap **rootHeaps();

    // The arena the root heaps are carved from, exposed for the arena-form test.
    static Arena &defaultArena();

    // Scans all registered root heaps and returns the first one whose isInclude(ptr)
    // returns true. Used by the global operator delete[] port (_ZdaPv, 0x7100e565b0).
    static Heap *findContainHeap(const void *ptr);

private:
    Heap *mCurrentHeap = nullptr;
    Heap *mRootHeaps[kMaxRootHeaps] = {};
    int mRootHeapNum = 0;
    Arena mDefaultArena;
};

} // namespace sead
