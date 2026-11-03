// sead::IDisposer — the base class every heap-managed object inherits from.
//
// When a heap is destroyed it walks its disposer list and calls ~IDisposer() on
// each registrant, giving objects a chance to clean up before their memory goes.
//
// Addresses observed in the boot closure (boot-callgraph.txt / boot-closure.txt):
//   0x7100e764f0  IDisposer()                — 96B, 2 call targets  (no-heap form)
//   0x7100e76550  IDisposer(Heap*, uint)     — 144B, 3 call targets (explicit heap)
//   0x7100e765e0  IDisposer tail-call target — reached via `b sead::IDisposer` in CalcInGSysCalc
//
// ENGINE_REFERENCE.md entry (0x7100e76550):
//   void __thiscall sead::IDisposer::IDisposer(IDisposer *this, Heap *param_1, uint param_2)
//   4 distinct call targets; 34 body lines
//
// Layout (confirmed by delegate_event.hpp offset comments and mDisposer[0x20] placeholder):
//   +0x00  vtable ptr   (8 bytes, implicit in ABI)
//   +0x08  mDisposerListNode  (ListNode = 16 bytes: mNext + mPrev)
//   +0x18  mDisposerHeap      (Heap*, 8 bytes)
//   total: 0x20 = 32 bytes
//
// Heap::appendDisposer_ (0x7100e7b29c, depth 3, 120B) — the call that links this node
// into the owning heap's disposer list — is deferred until Heap's internal CS and list
// fields are ported. The constructor stores the heap pointer today; the linkage follows.
#pragma once

#include "sead/list_node.hpp"

#include <cstdint>

namespace sead {

class Heap;

class IDisposer {
public:
    // 0x7100e764f0 — no-heap form: registers with the current heap from HeapMgr.
    IDisposer();
    // 0x7100e76550 — explicit heap form.  heapNullOption==1 → use current heap when
    // heap==nullptr; heapNullOption==0 → skip registration.
    explicit IDisposer(Heap *heap, unsigned int heapNullOption);
    virtual ~IDisposer();

    IDisposer(const IDisposer &) = delete;
    IDisposer &operator=(const IDisposer &) = delete;

    Heap *getDisposerHeap() const { return mDisposerHeap; }
    const ListNode &disposerListNode() const { return mDisposerListNode; }

protected:
    ListNode mDisposerListNode;    // +0x08 — node in the owning heap's disposer list
    Heap    *mDisposerHeap = nullptr; // +0x18 — back-pointer to the registering heap

private:
    friend class Heap; // appendDisposer_ / removeDisposer_ access mDisposerListNode directly
};

static_assert(sizeof(IDisposer) == 0x20,
              "sead::IDisposer must be exactly 0x20 bytes (vtable+ListNode+Heap*)");

} // namespace sead
