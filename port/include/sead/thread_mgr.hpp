// sead::ThreadMgr / sead::GlobalRandom — the two dependencies of Framework::initialize.
//
// ThreadMgr (ENGINE_REFERENCE.md):
//   0x7100e99e68  ThreadMgr()        76B   — allocates the current-thread TLS slot
//   0x7100e995d8  createInstance     136B
//   0x7100e99eb4  initialize         4B    — a jump to initMainThread_
//   0x7100e996b8  initMainThread_    184B, 9 calls: GetCurrentThread, ChangeThreadPriority,
//                 GetThreadId, the MainThread allocation, SetTlsValue x2, GetCurrentCoreNumber
// Behaviour from open-ead/sead (no license, written independently).
//
// Port differences: the instance is a static object, so instance() is never null (sead
// allocates it from the heap); the core-number TLS slot (sead::CoreInfo) is not ported.
//
// GlobalRandom stays a call-contract model: its internals are not observable from here.
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
// Keeps the list of live sead::Threads, the MainThread object standing for the thread that
// called initialize(), and which sead::Thread runs on each host thread (a TLS slot).
class ThreadMgr {
public:
    ThreadMgr();

    static ThreadMgr *createInstance(Heap *heap);
    static ThreadMgr *instance();

    // The reference's initialize takes the instance pointer and a heap. It creates the
    // MainThread in `heap` for the calling thread (replacing any previous one).
    static void initialize(ThreadMgr *mgr, Heap *heap);
    // Destroys the MainThread.
    void destroy();

    Heap *heap() const { return mHeap; }
    bool isInitialized() const { return mInitialized; }

    Thread *getMainThread() const { return mMainThread; }
    // The sead::Thread running on the calling host thread, or null.
    Thread *getCurrentThread() const;
    bool isMainThread() const { return mMainThread != nullptr && getCurrentThread() == mMainThread; }
    // Live non-main threads.
    std::size_t threadCount() const { return mList.size(); }

private:
    friend class Thread;

    void addThread_(ListNode *node);
    void removeThread_(ListNode *node);
    static void setCurrentThread_(Thread *thread);
    void initMainThread_(Heap *heap);
    void destroyMainThread_();

    Heap *mHeap = nullptr;
    bool mInitialized = false;
    List mList;
    CriticalSection mListCS;
    Thread *mMainThread = nullptr;
    Heap *mMainThreadHeap = nullptr; // where mMainThread's memory came from
    std::uint32_t mThreadPtrTLS = 0; // nn::os TLS slot holding the current sead::Thread*
    bool mHasTLS = false;
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
