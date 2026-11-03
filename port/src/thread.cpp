// sead::Thread — see port/include/sead/thread.hpp.
#include "sead/thread.hpp"
#include "sead/thread_mgr.hpp"

#include <cstdio>

namespace sead {

Thread::Thread(const char *name, Heap *heap, int priority, MessageQueue::BlockType blockType,
               MessageQueue::Element quitMsg, int stackSize, int messageQueueSize)
    : mName(name != nullptr ? name : ""), mStackSize(stackSize), mBlockType(blockType),
      mQuitMsg(quitMsg), mPriority(priority) {
    mMessageQueue.allocate(messageQueueSize, heap);
    mThreadInner = new nn::os::ThreadType();
    nn::os::CreateThread(mThreadInner, threadFunc_, this, nullptr,
                         static_cast<std::size_t>(stackSize), priority);
    nn::os::SetThreadName(mThreadInner, mName);
    ThreadMgr::instance()->addThread_(&mListNode);
}

Thread::~Thread() {
    ThreadMgr::instance()->removeThread_(&mListNode);

    const State s = mState.load();
    if (s == State::cRunning) {
        std::printf("sead::Thread[%s]: destroyed while running; quitting first\n", mName);
        quitAndWaitDoneSingleThread(false);
    } else if (s == State::cQuitting || s == State::cTerminated) {
        waitDone();
    }

    nn::os::DestroyThread(mThreadInner);
    delete mThreadInner;
    mThreadInner = nullptr;
    mMessageQueue.free();
}

bool Thread::sendMessage(MessageQueue::Element msg, MessageQueue::BlockType blockType) {
    if (msg == MessageQueue::cNullElement || isDone() || msg == mQuitMsg) return false;
    return mMessageQueue.push(msg, blockType);
}

MessageQueue::Element Thread::recvMessage(MessageQueue::BlockType blockType) {
    if (mState.load() == State::cQuitting) return MessageQueue::cNullElement;
    return mMessageQueue.pop(blockType);
}

bool Thread::start() {
    if (mState.load() != State::cInitialized) return false;
    nn::os::StartThread(mThreadInner);
    mId = nn::os::GetThreadId(mThreadInner);
    // The body may already have moved the state on; only promote from Initialized.
    State expected = State::cInitialized;
    mState.compare_exchange_strong(expected, State::cRunning);
    return true;
}

void Thread::quit(bool isJam) {
    if (isDone()) return;
    State expected = State::cRunning;
    mState.compare_exchange_strong(expected, State::cQuitting);
    if (isJam) mMessageQueue.jam(mQuitMsg, MessageQueue::BlockType::Blocking);
    else mMessageQueue.push(mQuitMsg, MessageQueue::BlockType::Blocking);
}

void Thread::waitDone() {
    const State s = mState.load();
    if (s == State::cInitialized || s == State::cReleased) return;
    nn::os::WaitThread(mThreadInner);
    mState = State::cReleased;
}

void Thread::quitAndWaitDoneSingleThread(bool isJam) {
    quit(isJam);
    waitDone();
}

void Thread::setPriority(int priority) {
    mPriority = priority;
    if (isActive()) nn::os::ChangeThreadPriority(mThreadInner, priority);
}

void Thread::setAffinity(std::uint64_t coreMask) {
    mAffinity = coreMask;
    nn::os::SetThreadCoreMask(mThreadInner, -1, coreMask);
}

void Thread::yield() { nn::os::YieldThread(); }

void Thread::sleep(TickSpan howLong) {
    nn::os::SleepThread(nn::os::ConvertToTimeSpan(howLong.toS64()));
}

void Thread::run_() {
    while (true) {
        const MessageQueue::Element msg = mMessageQueue.pop(mBlockType);
        if (msg == mQuitMsg) break;
        calc_(msg);
    }
}

void Thread::threadFunc_(void *arg) {
    auto *self = static_cast<Thread *>(arg);
    ThreadMgr::setCurrentThread_(self);
    self->mId = nn::os::GetThreadId(self->mThreadInner);
    // A quit() issued before the body started keeps the thread in Quitting.
    State expected = State::cInitialized;
    self->mState.compare_exchange_strong(expected, State::cRunning);
    self->run_();
    self->mState = State::cTerminated;
    ThreadMgr::setCurrentThread_(nullptr);
}

} // namespace sead
