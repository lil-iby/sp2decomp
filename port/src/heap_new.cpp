// Sead-heap-aware operator new / delete[] — ports of:
//   _ZnwmPN4sead4HeapEi  0x7100e5629c  scalar operator new  (sead::Heap*, int align)
//   _ZnamPN4sead4HeapEi  0x7100e56310  array  operator new  (sead::Heap*, int align)
//   _ZdaPv               0x7100e565b0  global operator delete[]
//
// Shape evidence (ENGINE_REFERENCE.md):
//   new[]  116 bytes, 2 named call targets — heap->tryAlloc + one null-guard
//   new    116 bytes, 4 named call targets — tryAlloc + align-up util + assert + return
//   delete 100 bytes, 3 named call targets + 1 indirect — findContainHeap + virt free + null check
//
// The 4-callee count for scalar new and 2-callee for array new likely reflects the scalar
// new logging an additional message on failure (the extra indirection + printf).
// For the port, both call tryAlloc and log on failure; the vtable dispatch is covered
// by the virtual tryAlloc call.
#include "sead/heap_new.hpp"
#include "sead/heap_mgr.hpp"

#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Placement new — scalar  (_ZnwmPN4sead4HeapEi, 0x7100e5629c)
// ---------------------------------------------------------------------------
void *operator new(std::size_t size, sead::Heap *heap, int align) {
    if (heap == nullptr) {
        std::printf("operator new(size=%llu): heap is null\n",
                    static_cast<unsigned long long>(size));
        return nullptr;
    }
    void *p = heap->tryAlloc(size, align);
    if (p == nullptr) {
        std::printf("operator new(size=%llu, align=%d): tryAlloc failed\n",
                    static_cast<unsigned long long>(size), align);
    }
    return p;
}

// ---------------------------------------------------------------------------
// Placement new — array  (_ZnamPN4sead4HeapEi, 0x7100e56310)
// ---------------------------------------------------------------------------
void *operator new[](std::size_t size, sead::Heap *heap, int align) {
    if (heap == nullptr) {
        std::printf("operator new[](size=%llu): heap is null\n",
                    static_cast<unsigned long long>(size));
        return nullptr;
    }
    void *p = heap->tryAlloc(size, align);
    if (p == nullptr) {
        std::printf("operator new[](size=%llu, align=%d): tryAlloc failed\n",
                    static_cast<unsigned long long>(size), align);
    }
    return p;
}

// ---------------------------------------------------------------------------
// Placement-delete counterparts (compiler calls these if a constructor throws
// after a successful placement new; never called directly).
// ---------------------------------------------------------------------------
void operator delete(void *ptr, sead::Heap *heap, int align) noexcept {
    (void)align;
    if (ptr != nullptr && heap != nullptr)
        heap->free(ptr);
}

void operator delete[](void *ptr, sead::Heap *heap, int align) noexcept {
    (void)align;
    if (ptr != nullptr && heap != nullptr)
        heap->free(ptr);
}

// ---------------------------------------------------------------------------
// Global operator delete[]  (_ZdaPv, 0x7100e565b0)
//
// The binary's body:
//   1. Null-check (returns early if ptr == nullptr)
//   2. Calls sead::HeapMgr::findContainHeap(ptr)  [3rd call target]
//   3. Dispatches heap->free(ptr) through the vtable  [indirect call, slot +0x38]
//
// For the port, we mirror that dispatch precisely. When findContainHeap returns
// nullptr (allocation was not from a sead heap — e.g. test harness stack/heap),
// we fall back to std::free so that host allocations are freed correctly.
// ---------------------------------------------------------------------------
void operator delete[](void *ptr) noexcept {
    if (ptr == nullptr) return;
    sead::Heap *heap = sead::HeapMgr::findContainHeap(ptr);
    if (heap != nullptr) {
        heap->free(ptr);
    } else {
        // Fallback: not owned by any sead heap — use the platform allocator directly.
        // std::free is safe here because non-sead allocations are backed by malloc.
        std::free(ptr);
    }
}
