// sead::IDisposer — port of 0x7100e764f0 (96B) and 0x7100e76550 (144B).
#include "sead/i_disposer.hpp"
#include "sead/heap.hpp"
#include "sead/heap_mgr.hpp"

namespace sead {

IDisposer::IDisposer() : IDisposer(nullptr, HeapNullOption::UseSpecifiedOrContainHeap) {}

IDisposer::IDisposer(Heap *heap, HeapNullOption option) {
    if (heap == nullptr) {
        switch (option) {
        case HeapNullOption::UseSpecifiedOrContainHeap:
            heap = HeapMgr::findContainHeap(this);
            break;
        case HeapNullOption::UseSpecifiedOrCurrentHeap:
            heap = HeapMgr::instance()->getCurrentHeap();
            break;
        case HeapNullOption::AlwaysUseSpecifiedHeap: // binary asserts; the port stays unregistered
        case HeapNullOption::DoNotAppendDisposerIfNoHeapSpecified:
            break;
        }
    }
    mDisposerHeap = heap;
    if (heap != nullptr) heap->appendDisposer_(this);
}

// An unlinked node means the heap was destroyed first and already detached us, so
// mDisposerHeap may be dangling and must not be dereferenced.
IDisposer::~IDisposer() {
    if (mDisposerHeap != nullptr && mDisposerListNode.isLinked()) {
        mDisposerHeap->removeDisposer_(this);
    }
    mDisposerHeap = nullptr;
}

} // namespace sead
