// heap_new_tests.cpp — contract tests for sead operator new/delete[] overloads.
//
// Covers:
//   operator new  (size_t, sead::Heap*, int)  — _ZnwmPN4sead4HeapEi  0x7100e5629c
//   operator new[](size_t, sead::Heap*, int)  — _ZnamPN4sead4HeapEi  0x7100e56310
//   operator delete[](void*)                  — _ZdaPv               0x7100e565b0
#include "sead/heap_new.hpp"
#include "sead/heap_mgr.hpp"
#include "sead/heap.hpp"

#include <cstdio>
#include <cstring>

namespace {
int g_failures = 0, g_checks = 0;

void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else        std::printf("  ok    %s\n", what);
}

// Local heap fixture — initialises HeapMgr so tests have a root heap to work with.
struct HeapFixture {
    HeapFixture()  { sead::HeapMgr::initialize(4 * 1024 * 1024); } // 4 MB
    ~HeapFixture() { sead::HeapMgr::destroy(); }
};
} // namespace

// ---------------------------------------------------------------------------
// Test 1 — scalar operator new allocates from the given heap
// ---------------------------------------------------------------------------
static void test_operator_new_scalar() {
    std::printf("\n[1] operator new(size, heap, align) — scalar\n");

    HeapFixture fix;
    sead::Heap *heap = sead::HeapMgr::getRootHeap(0);

    int *p = static_cast<int *>(operator new(sizeof(int), heap, alignof(int)));
    Check(p != nullptr, "scalar new returns non-null");
    if (p) {
        *p = 0xDEAD;
        Check(*p == 0xDEAD, "allocated memory is writable");
        // Free via heap directly (not delete[])
        heap->free(p);
    }
}

// ---------------------------------------------------------------------------
// Test 2 — array operator new[] allocates from the given heap
// ---------------------------------------------------------------------------
static void test_operator_new_array() {
    std::printf("\n[2] operator new[](size, heap, align) — array\n");

    HeapFixture fix;
    sead::Heap *heap = sead::HeapMgr::getRootHeap(0);

    constexpr int N = 8;
    int *arr = static_cast<int *>(operator new[](sizeof(int) * N, heap, alignof(int)));
    Check(arr != nullptr, "array new returns non-null");
    if (arr) {
        for (int i = 0; i < N; ++i) arr[i] = i;
        Check(arr[0] == 0 && arr[7] == 7, "array elements are writable");
        heap->free(arr);
    }
}

// ---------------------------------------------------------------------------
// Test 3 — operator new with null heap returns null (does not crash)
// ---------------------------------------------------------------------------
static void test_operator_new_null_heap() {
    std::printf("\n[3] operator new with null heap\n");

    void *p = operator new(64, nullptr, 8);
    Check(p == nullptr, "null heap returns nullptr");
    // No free needed — allocation failed.
}

// ---------------------------------------------------------------------------
// Test 4 — findContainHeap recognises sead-allocated pointer
// ---------------------------------------------------------------------------
static void test_find_contain_heap() {
    std::printf("\n[4] HeapMgr::findContainHeap after sead allocation\n");

    HeapFixture fix;
    sead::Heap *heap = sead::HeapMgr::getRootHeap(0);

    void *p = operator new(32, heap, 8);
    Check(p != nullptr, "allocation succeeded");
    if (p) {
        sead::Heap *found = sead::HeapMgr::findContainHeap(p);
        Check(found == heap, "findContainHeap returns the allocating heap");
        heap->free(p);
    }

    Check(sead::HeapMgr::findContainHeap(nullptr) == nullptr,
          "findContainHeap(nullptr) returns nullptr");
}

// ---------------------------------------------------------------------------
// Test 5 — global operator delete[] dispatches through the found heap
// ---------------------------------------------------------------------------
static void test_operator_delete_array_sead() {
    std::printf("\n[5] operator delete[](void*) — sead-heap pointer\n");

    HeapFixture fix;
    sead::Heap *heap = sead::HeapMgr::getRootHeap(0);

    const int before = heap->liveAllocationCount();
    void *arr = operator new[](256, heap, 8);
    Check(arr != nullptr, "allocation succeeded");
    if (arr) {
        const int during = heap->liveAllocationCount();
        Check(during == before + 1, "live count incremented after alloc");

        operator delete[](arr);  // calls our _ZdaPv port

        const int after = heap->liveAllocationCount();
        Check(after == before, "live count returns to baseline after delete[]");
    }
}

// ---------------------------------------------------------------------------
// Test 6 — placement-delete counterparts do not crash
// ---------------------------------------------------------------------------
static void test_placement_delete() {
    std::printf("\n[6] placement delete counterparts\n");

    HeapFixture fix;
    sead::Heap *heap = sead::HeapMgr::getRootHeap(0);

    void *p = operator new(16, heap, 8);
    Check(p != nullptr, "alloc for placement-delete test succeeded");
    if (p) {
        operator delete(p, heap, 8);   // should free via heap
        Check(true, "operator delete(ptr, heap, align) did not crash");
    }

    void *arr = operator new[](16, heap, 8);
    Check(arr != nullptr, "array alloc for placement-delete test succeeded");
    if (arr) {
        operator delete[](arr, heap, 8);
        Check(true, "operator delete[](ptr, heap, align) did not crash");
    }
}

// ---------------------------------------------------------------------------
int main() {
    std::printf("sead heap operator new/delete[] tests\n");
    std::printf("(_ZnwmPN4sead4HeapEi / _ZnamPN4sead4HeapEi / _ZdaPv)\n");

    test_operator_new_scalar();
    test_operator_new_array();
    test_operator_new_null_heap();
    test_find_contain_heap();
    test_operator_delete_array_sead();
    test_placement_delete();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
