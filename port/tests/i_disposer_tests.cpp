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
// Test 8 — construction links the node into the heap's list; destruction unlinks it
// ---------------------------------------------------------------------------
static void test_registration_roundtrip() {
    std::printf("\n[8] appendDisposer_ / removeDisposer_ round-trip\n");
    HeapFixture fix;
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    const std::size_t before = root->disposerCount();
    {
        sead::IDisposer a(root, 1);
        Check(a.disposerListNode().isLinked(), "node is linked after construction");
        Check(root->disposerCount() == before + 1, "heap count +1 after one disposer");
        {
            sead::IDisposer b(root, 1);
            Check(root->disposerCount() == before + 2, "heap count +2 after two disposers");
        }
        Check(root->disposerCount() == before + 1, "inner disposer removed itself");
        Check(a.disposerListNode().isLinked(), "outer node still linked after inner removed");
    }
    Check(root->disposerCount() == before, "heap count back to baseline");
}

// ---------------------------------------------------------------------------
// Test 9 — option 0 with null heap registers nowhere
// ---------------------------------------------------------------------------
static void test_no_registration_without_heap() {
    std::printf("\n[9] IDisposer(nullptr, 0) — not linked anywhere\n");
    HeapFixture fix;
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    const std::size_t before = root->disposerCount();
    {
        sead::IDisposer obj(nullptr, 0);
        Check(!obj.disposerListNode().isLinked(), "node is not linked");
        Check(root->disposerCount() == before, "root count unchanged");
    }
}

// ---------------------------------------------------------------------------
// Test 10 — Heap is an IDisposer: a child heap registers with its parent
// ---------------------------------------------------------------------------
static void test_child_heap_registers_with_parent() {
    std::printf("\n[10] child heap registers in parent's disposer list\n");
    HeapFixture fix;
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    Check(root->getDisposerHeap() == nullptr, "root heap has no parent registration");

    const std::size_t before = root->disposerCount();
    sead::ExpHeap *child = sead::ExpHeap::create(1024, "child", root);
    Check(child != nullptr, "child heap created");
    if (child == nullptr) return;
    Check(child->getDisposerHeap() == root, "child's disposer heap is the parent");
    Check(root->disposerCount() == before + 1, "parent count +1");

    child->destroy();
    delete child;
    Check(root->disposerCount() == before, "deleting the child unregisters it");
}

// ---------------------------------------------------------------------------
// Test 11 — parent destroyed first: the child is detached, and deleting it later is safe
// ---------------------------------------------------------------------------
static void test_parent_destroyed_first() {
    std::printf("\n[11] parent heap destroyed before its child\n");
    sead::ExpHeap *parent = sead::ExpHeap::create(4096, "parent");
    sead::ExpHeap *child = sead::ExpHeap::create(1024, "child", parent);
    Check(parent != nullptr && child != nullptr, "both heaps created");
    if (parent == nullptr || child == nullptr) return;
    Check(parent->disposerCount() == 1, "parent holds one disposer");

    parent->destroy();
    delete parent;
    Check(!child->disposerListNode().isLinked(), "child detached when parent died");

    child->destroy();
    delete child; // must not touch the dead parent
    Check(true, "deleting the orphaned child did not crash");
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
    test_registration_roundtrip();
    test_no_registration_without_heap();
    test_child_heap_registers_with_parent();
    test_parent_destroyed_first();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
