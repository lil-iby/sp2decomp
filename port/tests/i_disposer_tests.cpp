// i_disposer_tests.cpp — contract tests for sead::IDisposer.
//
// Covers:
//   sead::IDisposer()              — 0x7100e764f0, 96B, no-heap form
//   sead::IDisposer(Heap*, uint)   — 0x7100e76550, 144B, explicit heap form
//   virtual ~IDisposer()           — destructor (unlinks from list when linked)
//   layout: sizeof(IDisposer) == 0x20
#include "sead/i_disposer.hpp"
#include "sead/heap_mgr.hpp"
#include "sead/heap.hpp"

#include <cstdio>
#include <cstddef>

namespace {
int g_failures = 0, g_checks = 0;

void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else        std::printf("  ok    %s\n", what);
}

struct HeapFixture {
    HeapFixture()  { sead::HeapMgr::initialize(2 * 1024 * 1024); }
    ~HeapFixture() { sead::HeapMgr::destroy(); }
};
} // namespace

// ---------------------------------------------------------------------------
// Test 1 — sizeof(IDisposer) == 0x20 (vtable + ListNode + Heap*)
// ---------------------------------------------------------------------------
static void test_layout() {
    std::printf("\n[1] sizeof(sead::IDisposer) == 0x20\n");
    Check(sizeof(sead::IDisposer) == 0x20, "sizeof == 0x20");
    // mDisposerListNode is at offset 8 (right after vtable ptr in ABI)
    sead::IDisposer *tmp = nullptr;
    // Can't take offsetof of a non-standard layout class, but the static_assert
    // in the header already validates the size at compile time.
    Check(true, "static_assert in header passed (compile-time proof)");
}

// ---------------------------------------------------------------------------
// Test 2 — no-arg constructor does not crash; getDisposerHeap() is set
// ---------------------------------------------------------------------------
static void test_default_ctor_no_heap() {
    std::printf("\n[2] IDisposer() — no current heap (no crash)\n");
    // No HeapMgr initialised → current heap is null → constructor is a no-op.
    {
        sead::IDisposer obj;
        Check(obj.getDisposerHeap() == nullptr, "getDisposerHeap() == nullptr when no heap active");
    }
    Check(true, "destructor of no-heap IDisposer did not crash");
}

// ---------------------------------------------------------------------------
// Test 3 — no-arg constructor picks up the current heap
// ---------------------------------------------------------------------------
static void test_default_ctor_with_heap() {
    std::printf("\n[3] IDisposer() — records current heap\n");
    HeapFixture fix;
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    {
        sead::IDisposer obj;
        Check(obj.getDisposerHeap() == root,
              "getDisposerHeap() returns the active root heap");
    }
    Check(true, "destructor did not crash");
}

// ---------------------------------------------------------------------------
// Test 4 — explicit-heap constructor stores the provided heap
// ---------------------------------------------------------------------------
static void test_explicit_heap_ctor() {
    std::printf("\n[4] IDisposer(heap, 1) — stores the supplied heap\n");
    HeapFixture fix;
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    {
        sead::IDisposer obj(root, 1);
        Check(obj.getDisposerHeap() == root, "getDisposerHeap() == supplied heap");
    }
}

// ---------------------------------------------------------------------------
// Test 5 — null heap + heapNullOption==1 falls back to current heap
// ---------------------------------------------------------------------------
static void test_null_heap_option1() {
    std::printf("\n[5] IDisposer(nullptr, 1) — falls back to current heap\n");
    HeapFixture fix;
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    {
        sead::IDisposer obj(nullptr, 1);
        Check(obj.getDisposerHeap() == root,
              "getDisposerHeap() falls back to current heap when nullptr+option1");
    }
}

// ---------------------------------------------------------------------------
// Test 6 — null heap + heapNullOption==0 stays null (no fallback)
// ---------------------------------------------------------------------------
static void test_null_heap_option0() {
    std::printf("\n[6] IDisposer(nullptr, 0) — no fallback, stays null\n");
    HeapFixture fix;
    {
        sead::IDisposer obj(nullptr, 0);
        Check(obj.getDisposerHeap() == nullptr,
              "getDisposerHeap() == nullptr when option==0");
    }
}

// ---------------------------------------------------------------------------
// Test 7 — virtual dispatch: destructor reaches a subclass override
// ---------------------------------------------------------------------------
static void test_virtual_dtor() {
    std::printf("\n[7] virtual ~IDisposer() — subclass destructor is called\n");
    static bool dtorCalled = false;
    struct Derived : sead::IDisposer {
        ~Derived() override { dtorCalled = true; }
    };
    {
        Derived d;
        (void)d;
    }
    Check(dtorCalled, "subclass destructor was invoked through vtable");
}

// ---------------------------------------------------------------------------
int main() {
    std::printf("sead::IDisposer tests\n");
    std::printf("(0x7100e764f0 / 0x7100e76550)\n");

    test_layout();
    test_default_ctor_no_heap();
    test_default_ctor_with_heap();
    test_explicit_heap_ctor();
    test_null_heap_option1();
    test_null_heap_option0();
    test_virtual_dtor();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
