// sead::IDisposer — the base class every heap-managed object inherits from.
//
// When a heap is destroyed it walks its disposer list so registrants can clean up.
//
//   0x7100e764f0  IDisposer()                       96B, 2 calls: findContainHeap, appendDisposer_
//   0x7100e76550  IDisposer(Heap*, HeapNullOption)  144B, 3 calls: appendDisposer_,
//                                                   findContainHeap, getCurrentHeap
//
// The option values and field order were checked against open-ead/sead (a public
// reimplementation of the library). Its constructors call exactly the targets counted above,
// which is what makes the reference trustworthy here. Written independently, not copied.
//
// Layout (x64 / AArch64):
//   +0x00  vtable ptr
//   +0x08  mDisposerHeap
//   +0x10  mDisposerListNode  (ListNode: mNext, mPrev)
//   total 0x20
#pragma once

#include "sead/list_node.hpp"

#include <cstdint>

namespace sead {

class Heap;

class IDisposer {
public:
    // What to do when the constructor is given no heap.
    enum class HeapNullOption : std::uint32_t {
        AlwaysUseSpecifiedHeap = 0,               // a null heap is a caller error
        UseSpecifiedOrContainHeap = 1,            // heap whose memory holds `this`
        DoNotAppendDisposerIfNoHeapSpecified = 2, // stay unregistered
        UseSpecifiedOrCurrentHeap = 3,            // HeapMgr's current heap
    };

    // 0x7100e764f0 — same as IDisposer(nullptr, UseSpecifiedOrContainHeap).
    IDisposer();
    // 0x7100e76550
    explicit IDisposer(Heap *heap,
                       HeapNullOption option = HeapNullOption::UseSpecifiedOrCurrentHeap);
    virtual ~IDisposer();

    IDisposer(const IDisposer &) = delete;
    IDisposer &operator=(const IDisposer &) = delete;

    Heap *getDisposerHeap() const { return mDisposerHeap; }
    const ListNode &disposerListNode() const { return mDisposerListNode; }

protected:
    Heap    *mDisposerHeap = nullptr; // +0x08
    ListNode mDisposerListNode;       // +0x10

private:
    friend class Heap; // appendDisposer_ / removeDisposer_ splice mDisposerListNode
};

static_assert(sizeof(IDisposer) == 0x20,
              "sead::IDisposer must be exactly 0x20 bytes (vtable+Heap*+ListNode)");

} // namespace sead
