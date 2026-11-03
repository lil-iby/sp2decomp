// sead::ExpHeap — the heap layer over the host allocator.
//
// See port/include/sead/heap.hpp for the design rationale. The short version: the engine
// reaches a heap only through the 27-entry virtual interface enumerated in
// docs/HEAP_INTERFACE.md, and does not depend on ExpHeap's internal free-list layout, so
// this implements the INTERFACE over host allocation rather than reconstructing
// allocFromHead_/pushToFreeList_/adjust coalescing.
//
// The knowingly-different behaviours, recorded rather than hidden:
//   * adjust() is a no-op -- there is no sead free list to coalesce.
//   * isResizable() / isAdjustable() report false -- the host allocator does not resize.
//   * dump/dumpYAML/genInformation_ are inherited inert (heap-internal state is not modelled).
#include "sead/heap.hpp"
#include "sead/i_disposer.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

#if defined(_WIN32)
#  include <malloc.h>
#endif

namespace sead {

namespace {
// Round an alignment up to something the host allocator accepts.
std::size_t NormaliseAlign(std::size_t a) {
    if (a < alignof(void *)) return alignof(void *);
    // posix_memalign requires a power of two multiple of sizeof(void*); round up.
    std::size_t p = alignof(void *);
    while (p < a) p <<= 1;
    return p;
}
} // namespace

ExpHeap *ExpHeap::create(std::size_t size, const char *name, Heap *parent, int alignment,
                         HeapDirection direction, bool enableLock) {
    (void)alignment;
    (void)direction;
    (void)enableLock;
    auto *h = new (std::nothrow) ExpHeap(parent);
    if (h == nullptr) return nullptr;
    // The heap's "extent" is the requested size: the game treats it as the budget, and the
    // host allocator backs the actual bytes. mBuffer stays null, so blocks come from the host.
    if (!h->init(size, name, nullptr)) {
        delete h;
        return nullptr;
    }
    return h;
}

ExpHeap *ExpHeap::tryCreate(std::size_t size, const char *name, Heap *parent, int alignment,
                            HeapDirection direction, bool enableLock) {
    return create(size, name, parent, alignment, direction, enableLock);
}

ExpHeap *ExpHeap::create(void *buffer, std::size_t size, const char *name, Heap *parent,
                         bool enableLock) {
    (void)enableLock;
    if (buffer == nullptr || size == 0) return nullptr;
    auto *h = new (std::nothrow) ExpHeap(parent);
    if (h == nullptr) return nullptr;
    if (!h->init(size, name, buffer)) {
        delete h;
        return nullptr;
    }
    return h;
}

bool ExpHeap::init(std::size_t size, const char *name, void *buffer) {
    mName = name;
    mSize = size;
    mBuffer = buffer;
    mOwnsBuffer = false; // create() owns the object, not the buffer the caller passed
    mAllocCount = 0;
    mAllocated = 0;
    mDestroyed = false;
    mStartAddress = buffer != nullptr ? reinterpret_cast<std::uintptr_t>(buffer) : 0;
    return true;
}

void *ExpHeap::tryAlloc(std::size_t size, int alignment) {
    if (size == 0) return nullptr;
    if (mAllocCount >= kMaxTracked) return nullptr; // refuse rather than silently drop the record
    const std::size_t align = NormaliseAlign(alignment > 0 ? static_cast<std::size_t>(alignment) : 8);
    void *p = nullptr;
#if defined(_WIN32)
    p = _aligned_malloc(size, align);
#else
    if (posix_memalign(&p, align, size) != 0) p = nullptr;
#endif
    if (p == nullptr) return nullptr;
    std::memset(p, 0, size); // callers treat fresh heap memory as zeroed
    mAllocs[mAllocCount].ptr = p;
    mAllocs[mAllocCount].size = size;
    ++mAllocCount;
    mAllocated += size;
    return p;
}

void ExpHeap::free(void *ptr) {
    if (ptr == nullptr) return;
    for (int i = 0; i < mAllocCount; ++i) {
        if (mAllocs[i].ptr == ptr) {
            mAllocated -= mAllocs[i].size;
            // compact the record table
            mAllocs[i] = mAllocs[mAllocCount - 1];
            --mAllocCount;
#if defined(_WIN32)
            _aligned_free(ptr);
#else
            std::free(ptr);
#endif
            return;
        }
    }
    // Not ours: refuse to free rather than corrupting an unrelated allocation.
}

void ExpHeap::freeAll() {
    for (int i = 0; i < mAllocCount; ++i) {
#if defined(_WIN32)
        _aligned_free(mAllocs[i].ptr);
#else
        std::free(mAllocs[i].ptr);
#endif
    }
    mAllocCount = 0;
    mAllocated = 0;
}

std::size_t ExpHeap::getFreeSize() const {
    // The heap has a declared budget (mSize) and tracks what it has handed out. In a real
    // ExpHeap this would come from the free list; here it is the budget minus outstanding
    // allocations, which is the same quantity the boot path uses it for.
    if (mSize <= mAllocated) return 0;
    return mSize - mAllocated;
}

std::size_t ExpHeap::getMaxAllocatableSize(int alignment) {
    // vptr+0x80, the boot-path call. Distinct from getFreeSize: a host allocator can serve
    // any single allocation within the remaining budget, so the answer is the free size --
    // but it is computed and named separately so the interface stays honest about which
    // method the game called.
    (void)alignment;
    return getFreeSize();
}

bool ExpHeap::isInclude(const void *ptr) const {
    if (ptr == nullptr) return false;
    for (int i = 0; i < mAllocCount; ++i) {
        if (mAllocs[i].ptr == ptr) return true;
    }
    return false;
}

bool ExpHeap::isEmpty() const { return mAllocCount == 0; }

void ExpHeap::destroy() {
    // Free every outstanding allocation and mark the heap empty -- but leave the OBJECT USABLE.
    //
    // This matters and was caught by a segfault: Framework::initialize calls `th->destroy(th)`
    // on the ThreadMgr heap and the boot path then keeps using heap objects (GameFramework::
    // initialize queries the ROOT heap's getMaxAllocatableSize right afterwards). In the
    // binary those heaps are arena-resident and survive destroy(); an earlier version of this
    // set a destroyed flag that made subsequent queries read torn-down state, which crashed.
    if (mDestroyed) return;
    freeAll();
    if (mOwnsBuffer && mBuffer != nullptr) {
        std::free(mBuffer);
        mBuffer = nullptr;
    }
    mDestroyed = true; // records that destroy ran; queries remain valid
}

int ExpHeap::liveAllocationCount() const { return mAllocCount; }

// ---------------------------------------------------------------------------
// sead::Heap::appendDisposer_  (0x7100e7b29c, 120B, 4 call targets + 1 indirect)
//
// Call targets observed (ENGINE_REFERENCE.md):
//   1. CriticalSection::lock   — mCS.lock()
//   2. ListNode::insertFront_  — links disposer's node into mDisposerList
//   3. store disposer->mDisposerHeap = this  (named as a call in Ghidra's decompiler)
//   4. CriticalSection::unlock — mCS.unlock()
//   indirect: one vtable slot on Heap (not reached on boot path; left as no-op)
// ---------------------------------------------------------------------------
void Heap::appendDisposer_(IDisposer *disposer) {
    if (disposer == nullptr) return;
    mCS.lock();
    mDisposerList.pushBack(&disposer->mDisposerListNode);
    mCS.unlock();
}

Heap::~Heap() {
    mCS.lock();
    while (!mDisposerList.empty()) mDisposerList.root()->next()->erase_();
    mCS.unlock();
}

// Symmetric remove, called by ~IDisposer().
void Heap::removeDisposer_(IDisposer *disposer) {
    if (disposer == nullptr) return;
    mCS.lock();
    if (disposer->mDisposerListNode.isLinked())
        disposer->mDisposerListNode.erase_();
    mCS.unlock();
}

} // namespace sead
