// sead::ThreadMgr / sead::GlobalRandom — the two dependencies of Framework::initialize.
//
// Both are minimal: the decompiled body calls ThreadMgr::createInstance(heap) and
// ThreadMgr::initialize(instance, heap), then GlobalRandom::createInstance(rootHeap). What
// those do internally is not observable from this call site, so this models the CALL
// CONTRACT (an instance that can be created and initialised) rather than inventing thread
// or RNG semantics that would be guesses.
//
// That restraint is deliberate: the platform layer's nn::os provides real threading, and the
// engine's ThreadMgr sits on top of it. Porting its internals needs its own decompiled body,
// which is not part of the boot root's 13 dependencies.
#pragma once

#include "sead/list_node.hpp"
#include "sead/lock.hpp"

#include <cstddef>
#include <cstdint>

namespace sead {

class Heap;
class Thread;

// sead::ThreadMgr — created and initialised with a heap during Framework::initialize.
//
// Also keeps the list of live sead::Threads and which one is running on the calling host
// thread. The main-thread object (initMainThread_) is not ported yet, so getMainThread()
// is always null.
class ThreadMgr {
public:
    static ThreadMgr *createInstance(Heap *heap);
    static ThreadMgr *instance();

    // The reference's initialize takes the instance pointer and a heap.
    static void initialize(ThreadMgr *mgr, Heap *heap);

    Heap *heap() const { return mHeap; }
    bool isInitialized() const { return mInitialized; }

    Thread *getMainThread() const { return nullptr; }
    // The sead::Thread whose body is running on the calling host thread, or null.
    Thread *getCurrentThread() const;
    std::size_t threadCount() const { return mList.size(); }

private:
    friend class Thread;

    void addThread_(ListNode *node);
    void removeThread_(ListNode *node);
    static void setCurrentThread_(Thread *thread);

    Heap *mHeap = nullptr;
    bool mInitialized = false;
    List mList;
    CriticalSection mListCS;
};

// sead::GlobalRandom — the engine's shared RNG, created with the root heap.
class GlobalRandom {
public:
    static GlobalRandom *createInstance(Heap *heap);
    static GlobalRandom *instance();

    // A deterministic default seed, so behaviour is reproducible across runs. The original's
    // seed source is not observable from Framework::initialize.
    std::uint32_t next();

    void setSeed(std::uint32_t seed) { mState = seed ? seed : 1u; }

private:
    std::uint32_t mState = 1u;
};

} // namespace sead
