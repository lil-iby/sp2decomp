// i_disposer_tests.cpp — contract tests for sead::IDisposer.
//
// Covers:
//   sead::IDisposer()              — 0x7100e764f0, 96B, no-heap form
//   sead::IDisposer(Heap*, HeapNullOption) — 0x7100e76550, 144B
//   virtual ~IDisposer()           — destructor (unlinks from list when linked)
//   layout: sizeof(IDisposer) == 0x20
#include "sead/i_disposer.hpp"
#include "sead/heap_mgr.hpp"
#include "sead/heap.hpp"
#include "sead/heap_new.hpp"

#include "sead/lock.hpp"

#include <new>
#include <thread>

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

using Opt = sead::IDisposer::HeapNullOption;

// ---------------------------------------------------------------------------
// Test 1 — sizeof(IDisposer) == 0x20 (vtable + Heap* + ListNode)
// ---------------------------------------------------------------------------
static void test_layout() {
    std::printf("\n[1] sizeof(sead::IDisposer) == 0x20\n");
    Check(sizeof(sead::IDisposer) == 0x20, "sizeof == 0x20");
}

// ---------------------------------------------------------------------------
// Test 2 — no-arg constructor with no HeapMgr: unregistered, no crash
// ---------------------------------------------------------------------------
static void test_default_ctor_no_heap() {
    std::printf("\n[2] IDisposer() — no heap manager\n");
    {
        sead::IDisposer obj;
        Check(obj.getDisposerHeap() == nullptr, "getDisposerHeap() == nullptr");
    }
    Check(true, "destructor did not crash");
}

// ---------------------------------------------------------------------------
// Test 3 — no-arg constructor uses the heap whose memory holds the object
// ---------------------------------------------------------------------------
static void test_default_ctor_contain_heap() {
    std::printf("\n[3] IDisposer() — contain-heap lookup\n");
    HeapFixture fix;
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    {
        sead::IDisposer onStack;
        Check(onStack.getDisposerHeap() == nullptr,
              "object on the stack is in no heap -> unregistered");
    }
    void *mem = operator new(sizeof(sead::IDisposer), root, 8);
    Check(mem != nullptr, "allocated from root heap");
    if (mem == nullptr) return;
    auto *inHeap = new (mem) sead::IDisposer();
    Check(inHeap->getDisposerHeap() == root, "object inside root heap registers with root");
    Check(inHeap->disposerListNode().isLinked(), "and is linked into root's list");
    inHeap->~IDisposer();
    root->free(mem);
}

// ---------------------------------------------------------------------------
// Test 4 — an explicit heap is always used
// ---------------------------------------------------------------------------
static void test_explicit_heap_ctor() {
    std::printf("\n[4] IDisposer(heap) — stores the supplied heap\n");
    HeapFixture fix;
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    {
        sead::IDisposer obj(root, Opt::DoNotAppendDisposerIfNoHeapSpecified);
        Check(obj.getDisposerHeap() == root, "getDisposerHeap() == supplied heap");
    }
}

// ---------------------------------------------------------------------------
// Test 5 — UseSpecifiedOrCurrentHeap falls back to the current heap
// ---------------------------------------------------------------------------
static void test_null_heap_current() {
    std::printf("\n[5] IDisposer(nullptr, UseSpecifiedOrCurrentHeap)\n");
    HeapFixture fix;
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    {
        sead::IDisposer obj(nullptr, Opt::UseSpecifiedOrCurrentHeap);
        Check(obj.getDisposerHeap() == root, "falls back to the current heap");
    }
}

// ---------------------------------------------------------------------------
// Test 6 — DoNotAppendDisposerIfNoHeapSpecified stays unregistered
// ---------------------------------------------------------------------------
static void test_null_heap_do_not_append() {
    std::printf("\n[6] IDisposer(nullptr, DoNotAppendDisposerIfNoHeapSpecified)\n");
    HeapFixture fix;
    {
        sead::IDisposer obj(nullptr, Opt::DoNotAppendDisposerIfNoHeapSpecified);
        Check(obj.getDisposerHeap() == nullptr, "getDisposerHeap() == nullptr");
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
        sead::IDisposer a(root);
        Check(a.disposerListNode().isLinked(), "node is linked after construction");
        Check(root->disposerCount() == before + 1, "heap count +1 after one disposer");
        {
            sead::IDisposer b(root);
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
    std::printf("\n[9] IDisposer(nullptr, DoNotAppend...) — not linked anywhere\n");
    HeapFixture fix;
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    const std::size_t before = root->disposerCount();
    {
        sead::IDisposer obj(nullptr, Opt::DoNotAppendDisposerIfNoHeapSpecified);
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
// ---------------------------------------------------------------------------
// Test 12 — sead::CriticalSection: an IDisposer over a recursive nn::os mutex
// ---------------------------------------------------------------------------
static void test_critical_section() {
    std::printf("\n[12] sead::CriticalSection\n");
    Check(sizeof(sead::CriticalSection) == 0x40, "sizeof == 0x40 (IDisposer + MutexType)");

    HeapFixture fix;
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    const std::size_t before = root->disposerCount();

    void *mem = operator new(sizeof(sead::CriticalSection), root, 16);
    auto *cs = new (mem) sead::CriticalSection();
    Check(cs->getDisposerHeap() == root, "inside the root heap, it registers with root");
    Check(root->disposerCount() == before + 1, "root disposer count +1");

    cs->lock();
    cs->lock(); // recursive: must not deadlock
    bool otherGotIt = true;
    std::thread other([&] { otherGotIt = cs->tryLock(); });
    other.join();
    Check(!otherGotIt, "tryLock from another thread fails while held");
    cs->unlock();
    cs->unlock();

    std::thread again([&] {
        otherGotIt = cs->tryLock();
        if (otherGotIt) cs->unlock();
    });
    again.join();
    Check(otherGotIt, "tryLock from another thread succeeds once released");

    cs->~CriticalSection();
    root->free(mem);
    Check(root->disposerCount() == before, "destruction unregisters it");
}

int main() {
    std::printf("sead::IDisposer tests\n");
    std::printf("(0x7100e764f0 / 0x7100e76550)\n");

    test_layout();
    test_default_ctor_no_heap();
    test_default_ctor_contain_heap();
    test_explicit_heap_ctor();
    test_null_heap_current();
    test_null_heap_do_not_append();
    test_virtual_dtor();
    test_registration_roundtrip();
    test_no_registration_without_heap();
    test_child_heap_registers_with_parent();
    test_parent_destroyed_first();
    test_critical_section();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
