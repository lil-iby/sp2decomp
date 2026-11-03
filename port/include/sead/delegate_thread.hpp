// sead::DelegateThread — a sead::Thread whose calc_ forwards each message to a delegate.
//
// Binary: 0x7100e99804, depth 2, 92B, ENGINE_REFERENCE shows it opening with Thread::Thread.
// Lp::Sys::WorkerThread (0x710123c920) is built on it. Interface from open-ead/sead (no
// license, written independently). IDelegate2/Delegate2 are the minimum of sead's delegate
// family needed here.
#pragma once

#include "sead/thread.hpp"

namespace sead {

template <typename A1, typename A2>
class IDelegate2 {
public:
    virtual ~IDelegate2() = default;
    virtual void invoke(A1 a1, A2 a2) = 0;
};

// Binds a member function of T as an IDelegate2.
template <typename T, typename A1, typename A2>
class Delegate2 : public IDelegate2<A1, A2> {
public:
    using Method = void (T::*)(A1, A2);

    Delegate2(T *object, Method method) : mObject(object), mMethod(method) {}

    void invoke(A1 a1, A2 a2) override {
        if (mObject != nullptr && mMethod != nullptr) (mObject->*mMethod)(a1, a2);
    }

private:
    T *mObject;
    Method mMethod;
};

class DelegateThread : public Thread {
public:
    using Delegate = IDelegate2<Thread *, MessageQueue::Element>;

    DelegateThread(const char *name, Delegate *delegate, Heap *heap, int priority,
                   MessageQueue::BlockType blockType, MessageQueue::Element quitMsg,
                   int stackSize, int messageQueueSize)
        : Thread(name, heap, priority, blockType, quitMsg, stackSize, messageQueueSize),
          mDelegate(delegate) {}

protected:
    void calc_(MessageQueue::Element msg) override {
        if (mDelegate != nullptr) mDelegate->invoke(this, msg);
    }

    Delegate *mDelegate;
};

} // namespace sead
