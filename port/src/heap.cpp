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
    if (parent != nullptr) parent->pushBackChild_(h);
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
    if (parent != nullptr) parent->pushBackChild_(h);
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
        const auto start = reinterpret_cast<std::uintptr_t>(mAllocs[i].ptr);
        unlinkDisposersIn_(start, start + mAllocs[i].size);
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

// True when ptr lies anywhere inside a live block, not only at its start: sead tests an
// address range, and an IDisposer base or a member can sit partway into an allocation.
bool ExpHeap::isInclude(const void *ptr) const {
    if (ptr == nullptr) return false;
    const auto p = reinterpret_cast<std::uintptr_t>(ptr);
    for (int i = 0; i < mAllocCount; ++i) {
        const auto start = reinterpret_cast<std::uintptr_t>(mAllocs[i].ptr);
        if (p >= start && p < start + mAllocs[i].size) return true;
    }
    return false;
}

bool ExpHeap::isEmpty() const { return mAllocCount == 0; }

void ExpHeap::destroy() {
    // Free every outstanding allocation and mark the heap empty -- but leave the OBJECT USABLE:
    // in the binary heaps are arena-resident and survive destroy(), and callers keep querying
    // them afterwards. (The segfault that first exposed this came from Framework::initialize
    // calling destroy where the binary calls adjust; that call is fixed, the rule still holds.)
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
    if (mParent != nullptr) {
        mParent->mCS.lock();
        mChildNode.erase_();
        mParent->mCS.unlock();
        mParent = nullptr;
    }

    mCS.lock();
    while (!mDisposerList.empty()) mDisposerList.root()->next()->erase_();
    // Children outlive us (HeapMgr::destroy deletes only root heaps): orphan them.
    while (!mChildren.empty()) {
        auto *node = static_cast<ChildNode *>(mChildren.root()->next());
        node->heap->mParent = nullptr;
        node->erase_();
    }
    mCS.unlock();
}

void Heap::unlinkDisposersIn_(std::uintptr_t start, std::uintptr_t end) {
    mCS.lock();
    ListNode *n = mDisposerList.root()->next();
    while (n != mDisposerList.root()) {
        ListNode *next = n->next();
        const auto addr = reinterpret_cast<std::uintptr_t>(n);
        if (addr >= start && addr < end) n->erase_();
        n = next;
    }
    mCS.unlock();
}

void Heap::pushBackChild_(Heap *child) {
    if (child == nullptr || child == this) return;
    mCS.lock();
    mChildren.pushBack(&child->mChildNode);
    child->mParent = this;
    mCS.unlock();
}

Heap *Heap::findContainHeap_(const void *ptr) {
    if (ptr == nullptr) return nullptr;
    Heap *found = nullptr;
    mCS.lock();
    for (ListNode *n = mChildren.root()->next(); n != mChildren.root(); n = n->next()) {
        found = static_cast<ChildNode *>(n)->heap->findContainHeap_(ptr);
        if (found != nullptr) break;
    }
    mCS.unlock();
    if (found == nullptr && isInclude(ptr)) found = this;
    return found;
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
