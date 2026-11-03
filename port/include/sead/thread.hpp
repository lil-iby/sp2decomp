// sead::Thread — a worker thread that loops on its own message queue.
//
// Binary addresses (ENGINE_REFERENCE.md):
//   0x7100e98f50  Thread::Thread        depth 3, 476B, 13 calls (opens with IDisposer ctor)
//   0x7100e992cc  Thread::setAffinity   depth 2, 56B
//   0x7100e99434  Thread::sleep         depth 2, 20B, 3 calls
// Behaviour from open-ead/sead (no license, written independently).
//
// Lifecycle: construct (suspended) -> start() -> run_() pops messages and passes each to
// calc_() until the quit message arrives -> quit() + waitDone(). A subclass implements
// calc_(). Always quit and waitDone before destroying a running thread: once the subclass
// destructor has run, its calc_ is gone.
//
// Not ported: the hostio/INamable bases, stack canary checks, the FindContainHeapCache and
// the main-thread object. The thread stack and nn::os::ThreadType are host-allocated
// rather than taken from `heap`, because the host thread owns its own stack.
#pragma once

#include "nn/os.hpp"
#include "sead/i_disposer.hpp"
#include "sead/list_node.hpp"
#include "sead/message_queue.hpp"
#include "sead/tick_span.hpp"

#include <atomic>
#include <cstdint>

namespace sead {

class Heap;

class Thread : public IDisposer {
public:
    enum class State { cInitialized = 0, cRunning = 1, cQuitting = 2, cTerminated = 3, cReleased = 4 };

    static constexpr int cDefaultPriority = 0x10;

    Thread(const char *name, Heap *heap, int priority, MessageQueue::BlockType blockType,
           MessageQueue::Element quitMsg, int stackSize, int messageQueueSize);
    ~Thread() override;

    Thread(const Thread &) = delete;
    Thread &operator=(const Thread &) = delete;

    virtual void destroy() { waitDone(); }

    virtual bool sendMessage(MessageQueue::Element msg, MessageQueue::BlockType blockType);
    virtual MessageQueue::Element recvMessage(MessageQueue::BlockType blockType);
    virtual const MessageQueue &getMessageQueue() const { return mMessageQueue; }

    virtual bool start();
    // Asks the loop to stop. is_jam puts the quit message ahead of pending messages.
    virtual void quit(bool isJam);
    virtual void waitDone();
    virtual void quitAndDestroySingleThread(bool isJam) { quitAndWaitDoneSingleThread(isJam); }
    virtual void quitAndWaitDoneSingleThread(bool isJam);

    virtual void setPriority(int priority);
    virtual int getPriority() const { return mPriority; }
    virtual MessageQueue::BlockType getBlockType() const { return mBlockType; }
    virtual int getStackSize() const { return mStackSize; }

    const char *getName() const { return mName; }
    std::uint64_t getId() const { return mId.load(); }
    State getState() const { return mState.load(); }
    bool isDone() const {
        const State s = mState.load();
        return s == State::cTerminated || s == State::cReleased;
    }
    bool isActive() const {
        const State s = mState.load();
        return s == State::cRunning || s == State::cQuitting;
    }

    std::uint64_t getAffinity() const { return mAffinity; }
    // Core mask; bit n = core n. The host scheduler ignores it.
    void setAffinity(std::uint64_t coreMask);

    static void yield();
    static void sleep(TickSpan howLong);

protected:
    // Main-thread form: adopts an already running thread (`inner`, null = the calling
    // thread) instead of creating one, and is not added to ThreadMgr's list.
    Thread(Heap *heap, nn::os::ThreadType *inner, std::uint64_t id);

    virtual void run_();
    virtual void calc_(MessageQueue::Element msg) = 0;

    MessageQueue mMessageQueue;

private:
    static void threadFunc_(void *arg);

    const char *mName;
    int mStackSize;
    ListNode mListNode;
    MessageQueue::BlockType mBlockType;
    MessageQueue::Element mQuitMsg;
    std::atomic<std::uint64_t> mId{0}; // written by start() and by the thread body
    std::atomic<State> mState{State::cInitialized};
    std::uint64_t mAffinity = 1; // CoreId::cMain
    nn::os::ThreadType *mThreadInner = nullptr;
    int mPriority;
};

// The thread that called ThreadMgr::initialize. It already runs, so the lifecycle calls
// that would start, stop or join it are refused (sead asserts; the port ignores them).
class MainThread : public Thread {
public:
    MainThread(Heap *heap, nn::os::ThreadType *inner, std::uint64_t id) : Thread(heap, inner, id) {}

    void destroy() override {}
    void quit(bool) override {}
    void waitDone() override {}
    void quitAndDestroySingleThread(bool) override {}
    void setPriority(int) override {}

protected:
    void calc_(MessageQueue::Element) override {}
};

} // namespace sead
