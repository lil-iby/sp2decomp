// sead::IDisposer — port of 0x7100e764f0 (96B) and 0x7100e76550 (144B).
//
// The constructor's 4 call targets (ENGINE_REFERENCE.md):
//   1. sead::HeapMgr::getCurrentHeap() — called when heap param is null / no-heap form
//   2. sead::HeapMgr::getCurrentHeap() — second access (the binary reads TLS slot twice)
//   3. sead::Heap::appendDisposer_()   — links this node into the heap's disposer list
//   4. One of the ListNode operations  — used inside appendDisposer_
//
// Heap::appendDisposer_ (0x7100e7b29c, depth 3, 120B) — requires Heap's internal
// CriticalSection and disposer-list head (mDisposerList), which are not yet ported.
// The constructor stores the owning heap today; mDisposerListNode linkage follows once
// those fields are added to sead::Heap.
#include "sead/i_disposer.hpp"
#include "sead/heap_mgr.hpp"

#include <cstdio>

namespace sead {

// 0x7100e764f0 — no-heap form (2 call targets: getCurrentHeap + appendDisposer_).
IDisposer::IDisposer()
    : mDisposerListNode(), mDisposerHeap(nullptr) {
    mDisposerHeap = HeapMgr::instance() ? HeapMgr::instance()->getCurrentHeap() : nullptr;
    // appendDisposer_ deferred — see file header.
}

// 0x7100e76550 — explicit heap form (3 call targets: getCurrentHeap + appendDisposer_ + 1 more).
// heapNullOption == 1 → fall back to current heap when heap arg is nullptr.
// heapNullOption == 0 → skip registration entirely when heap is null.
IDisposer::IDisposer(Heap *heap, unsigned int heapNullOption)
    : mDisposerListNode(), mDisposerHeap(nullptr) {
    if (heap == nullptr && heapNullOption != 0) {
        heap = HeapMgr::instance() ? HeapMgr::instance()->getCurrentHeap() : nullptr;
    }
    mDisposerHeap = heap;
    // appendDisposer_ deferred — see file header.
}

// Virtual destructor: unlink from heap's disposer list if we were ever linked.
IDisposer::~IDisposer() {
    if (mDisposerListNode.isLinked()) {
        mDisposerListNode.erase_();
    }
    mDisposerHeap = nullptr;
}

} // namespace sead
