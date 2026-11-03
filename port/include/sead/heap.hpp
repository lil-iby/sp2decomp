// sead::Heap / sead::ExpHeap — the heap layer, implemented over the host allocator.
//
// DESIGN, from HEAP_INTERFACE.md and HEAP_LAYER_SCOPE.md:
//   The engine reaches a heap only through virtual dispatch (27 entries in ExpHeap's
//   vtable, enumerated from the binary). It does NOT depend on ExpHeap's internal free-list
//   layout, so this implements the INTERFACE over the host allocator rather than
//   reconstructing allocFromHead_/pushToFreeList_/adjust coalescing.
//
//   That is the same strategy as the platform layer (port/src/os.cpp implements nn::os over
//   host primitives instead of reimplementing Horizon). The one deliberately unfaithful
//   choice is ExpHeap's block layout, and it is recorded as such in HEAP_LAYER_SCOPE.md §4.
//
//   The case that would break this: anything reading heap-internal state -- walking
//   MemBlocks, dumpYAML, or depending on fragmentation or exact addresses. Nothing on the
//   boot path does; dump/dumpYAML/genInformation_ are inert here and flagged as such.
//
// TWO NAMESPACES: the binary contains both sead:: and pead:: heap hierarchies. The boot
// path calls sead:: (see HEAP_LAYER_SCOPE.md §1), so this is sead::.
#pragma once

#include "sead/i_disposer.hpp"
#include "sead/list_node.hpp"
#include "sead/lock.hpp"

#include <cstddef>
#include <cstdint>

namespace sead {

class Heap;
class WriteStream;
namespace hostio {
class Context;
}

// Free-list direction, as the binary's CreateArg uses it.
enum class HeapDirection { eForward = 0, eReverse = 1 };

// The argument struct ExpHeap::create takes. Modelled from the decompiled call:
//   ExpHeap::create(size, "sead::SystemManagers", parent, align, direction, enableLock)
struct HeapCreateArg {
    const char *name = nullptr;
};

// sead::Heap — the base class. The vtable below mirrors the 27 entries enumerated in
// HEAP_INTERFACE.md, with the boot-path ones marked.
// sead::Heap is itself an IDisposer: the binary's Heap ctor (0x7100e7ada8) opens with
//   IDisposer::IDisposer((IDisposer *)this, param_2, 1);
// so a child heap registers in its parent's disposer list. A root heap has no parent and
// registers nowhere.
class Heap : public IDisposer {
public:
    explicit Heap(Heap *parent = nullptr)
        : IDisposer(parent, HeapNullOption::UseSpecifiedOrContainHeap) {}
    // Detaches every disposer still registered here, so none is left pointing into a
    // destroyed list. The disposers themselves are not destroyed (they are not owned).
    ~Heap() override;

    // Number of disposers currently registered. Test hook, like liveAllocationCount.
    std::size_t disposerCount() const { return mDisposerList.size(); }

    // --- RTTI (vptr+0x10, +0x18) -------------------------------------------------
    virtual bool checkDerivedRuntimeTypeInfo(const void *typeInfo) const { (void)typeInfo; return false; }
    virtual const void *getRuntimeTypeInfo() const { return nullptr; }

    // --- lifecycle (vptr+0x20, +0x28) --------------------------------------------
    virtual void destroy() {}
    // vptr+0x28. The boot path reaches this. In the real ExpHeap it coalesces adjacent free
    // blocks; with a host allocator there is nothing to coalesce, so it is a no-op that
    // reports success. Recorded as a knowingly-different behaviour.
    virtual void adjust() {}

    // --- allocation (vptr+0x30 .. +0x58) -----------------------------------------
    virtual void *tryAlloc(std::size_t size, int alignment) = 0;
    virtual void free(void *ptr) = 0;
    virtual bool resizeFront(void *ptr, std::size_t size) { (void)ptr; (void)size; return false; }
    virtual bool resizeBack(void *ptr, std::size_t size) { (void)ptr; (void)size; return false; }
    virtual void *tryRealloc(void *ptr, std::size_t size, int alignment) { (void)ptr; (void)size; (void)alignment; return nullptr; }
    virtual void freeAll() {}

    // --- extent (vptr+0x60 .. +0x78) ---------------------------------------------
    virtual std::uintptr_t getStartAddress() const { return mStartAddress; }
    virtual std::uintptr_t getEndAddress() const { return mStartAddress + mSize; }
    virtual std::size_t getSize() const { return mSize; }
    virtual std::size_t getFreeSize() const = 0;

    // --- vptr+0x80, THE BOOT-PATH CALL -------------------------------------------
    // "Max allocatable size at the given alignment", NOT getFreeSize. The boot path does
    //   (**(code **)(*heap + 0x80))(heap, 8)
    // to size each system-manager heap, so this must be its own method.
    virtual std::size_t getMaxAllocatableSize(int alignment) = 0;

    // --- capability queries (vptr+0x88 .. +0xa8) ---------------------------------
    virtual bool isInclude(const void *ptr) const = 0;
    virtual bool isEmpty() const = 0;
    virtual bool isFreeable() const = 0;
    virtual bool isResizable() const = 0;
    virtual bool isAdjustable() const = 0;

    // --- diagnostics (vptr+0xb0 .. +0xc0): INERT ---------------------------------
    // Nothing on the boot path calls these, and dumpYAML is exactly the "reads
    // heap-internal state" case the interface shortcut cannot satisfy. Left inert rather
    // than faked.
    virtual void dump() const {}
    virtual void dumpYAML(WriteStream &stream, int n) const { (void)stream; (void)n; }
    virtual void genInformation_(hostio::Context *ctx) const { (void)ctx; }

    // --- inherited from Heap, present in ExpHeap's vtable at +0xc8/+0xd0 ----------
    virtual void makeMetaString_(void *buf) const { (void)buf; }
    virtual void pushBackChild_(Heap *child) { (void)child; }

    // Live allocations outstanding. Not part of the original's interface -- added so tests
    // can verify accounting through a base pointer without downcasting.
    virtual int liveAllocationCount() const { return 0; }

    const char *name() const { return mName; }

    // --- disposer-list management (0x7100e7b29c, 120B) ---------------------------
    // Called by IDisposer's constructor to register `disposer` with this heap.
    // When this heap is destroyed it walks mDisposerList and calls each ~IDisposer().
    //
    // appendDisposer_ call targets (ENGINE_REFERENCE.md §appendDisposer_):
    //   lock   → mCS.lock()
    //   insert → ListNode::insertFront_ (links disposer->mDisposerListNode into mDisposerList)
    //   store  → disposer->mDisposerHeap = this  (back-pointer; counts as the 3rd named target)
    //   unlock → mCS.unlock()
    //   1 indirect: vtable slot on Heap (resize-notify or similar; left as no-op here)
    void appendDisposer_(IDisposer *disposer);
    // Symmetric remove, called by ~IDisposer() before self-destruction.
    void removeDisposer_(IDisposer *disposer);

protected:
    const char *mName = nullptr;
    std::uintptr_t mStartAddress = 0;
    std::size_t mSize = 0;

    // Added to support disposer-list registration (appendDisposer_ / removeDisposer_).
    CriticalSection mCS;          // thread safety for disposer list mutations
    List            mDisposerList; // sentinel-head list of IDisposer nodes
};

// sead::ExpHeap — the concrete heap the game instantiates per system manager.
//
// Backed by the host allocator. Every allocation is tracked so free(), freeAll(),
// isInclude() and the size queries can answer truthfully.
class ExpHeap : public Heap {
public:
    static constexpr int kMaxTracked = 4096;

    explicit ExpHeap(Heap *parent = nullptr) : Heap(parent) {}

    // ExpHeap::create @ 0x7100e76784. The binary's signature is
    //   create(size, name, parent, alignment, direction, enableLock)
    // and it returns a heap the caller then installs as current.
    static ExpHeap *create(std::size_t size,
                           const char *name,
                           Heap *parent = nullptr,
                           int alignment = 8,
                           HeapDirection direction = HeapDirection::eForward,
                           bool enableLock = true);
    static ExpHeap *tryCreate(std::size_t size, const char *name, Heap *parent = nullptr,
                              int alignment = 8,
                              HeapDirection direction = HeapDirection::eForward,
                              bool enableLock = true);

    // In-place construction over a caller-provided buffer (the second create overload the
    // binary provides at 0x7100e76c64).
    static ExpHeap *create(void *buffer, std::size_t size, const char *name,
                           Heap *parent = nullptr, bool enableLock = true);

    bool init(std::size_t size, const char *name, void *buffer);

    // --- Heap interface ----------------------------------------------------------
    void *tryAlloc(std::size_t size, int alignment) override;
    void free(void *ptr) override;
    void freeAll() override;
    std::size_t getFreeSize() const override;
    std::size_t getMaxAllocatableSize(int alignment) override;
    bool isInclude(const void *ptr) const override;
    bool isEmpty() const override;
    bool isFreeable() const override { return true; }
    bool isResizable() const override { return false; } // the host allocator does not resize
    bool isAdjustable() const override { return false; }
    void destroy() override;

    // Reporting for tests.
    int liveAllocationCount() const override;

private:
    struct Alloc {
        void *ptr;
        std::size_t size;
    };
    Alloc mAllocs[kMaxTracked];
    int mAllocCount = 0;
    std::size_t mAllocated = 0;
    bool mOwnsBuffer = false;
    void *mBuffer = nullptr;
    bool mDestroyed = false;
};

} // namespace sead
