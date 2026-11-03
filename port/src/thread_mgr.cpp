// sead::ThreadMgr / sead::GlobalRandom — see port/include/sead/thread_mgr.hpp.
#include "sead/thread_mgr.hpp"
#include "sead/heap.hpp"
#include "sead/thread.hpp"
#include "nn/os.hpp"

#include <new>

namespace sead {

namespace {
ThreadMgr gThreadMgr;
GlobalRandom gGlobalRandom;
} // namespace

// 0x7100e99e68 — the only work besides member setup is reserving the current-thread slot.
ThreadMgr::ThreadMgr() { mHasTLS = nn::os::AllocateTlsSlot(&mThreadPtrTLS, nullptr) == 0; }

Thread *ThreadMgr::getCurrentThread() const {
    return mHasTLS ? static_cast<Thread *>(nn::os::GetTlsValue(mThreadPtrTLS)) : nullptr;
}

void ThreadMgr::setCurrentThread_(Thread *thread) {
    if (gThreadMgr.mHasTLS) nn::os::SetTlsValue(gThreadMgr.mThreadPtrTLS, thread);
}

// 0x7100e996b8. The port passes a null ThreadType for "the calling thread": its
// nn::os::GetCurrentThread returns an id, not a ThreadType*, and every nn::os thread query
// treats null as the current thread.
void ThreadMgr::initMainThread_(Heap *heap) {
    destroyMainThread_();
    if (heap == nullptr) return;

    nn::os::ChangeThreadPriority(nullptr, Thread::cDefaultPriority);
    const std::uint64_t id = nn::os::GetThreadId(nullptr);

    void *mem = heap->tryAlloc(sizeof(MainThread), alignof(MainThread));
    if (mem == nullptr) return;
    mMainThread = new (mem) MainThread(heap, nullptr, id);
    mMainThreadHeap = heap;
    setCurrentThread_(mMainThread);
}

void ThreadMgr::destroyMainThread_() {
    if (mMainThread == nullptr) return;
    if (getCurrentThread() == mMainThread) setCurrentThread_(nullptr);
    // mMainThread stays set during the destructor: ~Thread uses it to recognise the main
    // thread and skip the parts that only apply to threads it created.
    mMainThread->~Thread();
    if (mMainThreadHeap != nullptr) mMainThreadHeap->free(mMainThread);
    mMainThread = nullptr;
    mMainThreadHeap = nullptr;
}

void ThreadMgr::destroy() { destroyMainThread_(); }

void ThreadMgr::addThread_(ListNode *node) {
    mListCS.lock();
    mList.pushBack(node);
    mListCS.unlock();
}

void ThreadMgr::removeThread_(ListNode *node) {
    mListCS.lock();
    if (node->isLinked()) node->erase_();
    mListCS.unlock();
}

ThreadMgr *ThreadMgr::createInstance(Heap *heap) {
    gThreadMgr.mHeap = heap;
    gThreadMgr.mInitialized = false;
    return &gThreadMgr;
}

ThreadMgr *ThreadMgr::instance() { return &gThreadMgr; }

// 0x7100e99eb4: 4 bytes, a tail call into initMainThread_.
void ThreadMgr::initialize(ThreadMgr *mgr, Heap *heap) {
    if (mgr == nullptr) return;
    mgr->mHeap = heap;
    mgr->mInitialized = true;
    mgr->initMainThread_(heap);
}

GlobalRandom *GlobalRandom::createInstance(Heap *heap) {
    (void)heap; // the RNG does not allocate; the heap argument is accepted for fidelity
    gGlobalRandom.mState = 1u;
    return &gGlobalRandom;
}

GlobalRandom *GlobalRandom::instance() { return &gGlobalRandom; }

std::uint32_t GlobalRandom::next() {
    // xorshift32 -- a deterministic generator. The original's algorithm is not observable
    // from Framework::initialize, so this provides *a* reproducible sequence rather than a
    // claim to match the original's stream.
    std::uint32_t x = gGlobalRandom.mState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    gGlobalRandom.mState = x;
    return x;
}

} // namespace sead
