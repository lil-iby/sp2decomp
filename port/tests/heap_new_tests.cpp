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
#include <new>

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
// Test 7 — blocks from a grandchild heap are found and delete[] returns them there
// ---------------------------------------------------------------------------
static void test_child_heap_lookup() {
    std::printf("\n[7] findContainHeap / delete[] through child heaps\n");

    HeapFixture fix;
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    sead::ExpHeap *child = sead::ExpHeap::create(0x10000, "child", root);
    sead::ExpHeap *grandchild = sead::ExpHeap::create(0x1000, "grandchild", child);
    Check(child != nullptr && grandchild != nullptr, "child and grandchild heaps created");
    if (child == nullptr || grandchild == nullptr) return;

    Check(child->getParent() == root && grandchild->getParent() == child, "parents recorded");
    Check(root->childCount() == 1 && child->childCount() == 1, "children recorded");

    void *arr = operator new[](64, grandchild, 8);
    Check(sead::HeapMgr::findContainHeap(arr) == grandchild,
          "a grandchild's block is found in the grandchild, not missed");

    operator delete[](arr); // previously fell back to std::free on _aligned_malloc memory
    Check(grandchild->liveAllocationCount() == 0, "delete[] returned the block to the grandchild");

    grandchild->destroy();
    delete grandchild;
    Check(child->childCount() == 0, "a deleted child leaves its parent's list");
    child->destroy();
    delete child;
}

// ---------------------------------------------------------------------------
// Test 8 — an address inside a block (not its start) belongs to the heap
// ---------------------------------------------------------------------------
static void test_interior_pointer() {
    std::printf("\n[8] interior pointer\n");

    HeapFixture fix;
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    auto *p = static_cast<unsigned char *>(operator new(64, root, 8));
    Check(sead::HeapMgr::findContainHeap(p + 10) == root, "ptr + 10 is inside the root heap");
    Check(sead::HeapMgr::findContainHeap(p + 64) != root, "one past the end is not");
    root->free(p);
}

// ---------------------------------------------------------------------------
// Test 9 — a parent deleted before its child orphans the child safely
// ---------------------------------------------------------------------------
static void test_parent_deleted_first() {
    std::printf("\n[9] parent heap deleted before its child\n");

    sead::ExpHeap *parent = sead::ExpHeap::create(0x1000, "parent");
    sead::ExpHeap *child = sead::ExpHeap::create(0x100, "child", parent);
    if (parent == nullptr || child == nullptr) { Check(false, "heaps created"); return; }

    parent->destroy();
    delete parent;
    Check(child->getParent() == nullptr, "child no longer points at the dead parent");
    child->destroy();
    delete child;
    Check(true, "deleting the orphan did not touch the dead parent");
}

// ---------------------------------------------------------------------------
// Test 10 — freeAll unlinks disposers living in the freed blocks
// ---------------------------------------------------------------------------
static void test_free_all_unlinks_disposers() {
    std::printf("\n[10] freeAll with a registered disposer inside the heap\n");

    HeapFixture fix;
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    const std::size_t before = root->disposerCount();

    void *mem = operator new(sizeof(sead::IDisposer), root, 8);
    new (mem) sead::IDisposer(); // contain-heap lookup registers it with root
    Check(root->disposerCount() == before + 1, "the in-heap disposer registered with root");

    root->freeAll(); // frees the block without running the destructor
    Check(root->disposerCount() == before, "freeAll unlinked it before releasing the memory");
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
    test_child_heap_lookup();
    test_interior_pointer();
    test_parent_deleted_first();
    test_free_all_unlinks_disposers();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
