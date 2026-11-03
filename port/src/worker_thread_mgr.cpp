// Lp::Sys::WorkerThreadMgr — over sead::DelegateThread, as in the binary.
//
// The boot closure shows the chain createWorker (0x710123d4a0) -> internal createWorker
// (0x710123d588, 536B) -> Lp::Sys::WorkerThread ctor (0x710123c920) -> sead::DelegateThread
// (0x7100e99804) -> sead::Thread. The worker thread's body is the delegate below:
//   enqueue  — adds the task to the pending batch
//   run      — sends the "run" message; the worker drains the batch, then signals done
// Lp::Sys::WorkerThread itself is game code with no public reference, so its exact fields
// are not reproduced; this keeps the observed behaviour on the real sead primitives.
#include "lp/worker_thread_mgr.hpp"

#include "sead/delegate_thread.hpp"
#include "sead/event.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <queue>

namespace Lp::Sys {

namespace {
using Element = sead::MessageQueue::Element;
constexpr Element kRunMsg = 1;
constexpr Element kQuitMsg = 0x7FFFFFFF;
} // namespace

struct WorkerThreadMgr::Impl {
    struct QueuedTask {
        WorkerTask::Fn fn;
        void *arg;
    };

    // Runs on the worker thread for every message it receives.
    void onMessage(sead::Thread *thread, Element msg) {
        (void)thread;
        if (msg != kRunMsg) return;
        int ran = 0;
        while (true) {
            QueuedTask task{};
            {
                std::lock_guard<std::mutex> lock(mx);
                if (taskQueue.empty()) break;
                task = taskQueue.front();
                taskQueue.pop();
            }
            task.fn(task.arg);
            ++ran;
        }
        completed = ran;
        done = true;
        batchDone.setSignal();
    }

    sead::Delegate2<Impl, sead::Thread *, Element> delegate{this, &Impl::onMessage};
    sead::DelegateThread *thread = nullptr;

    std::mutex mx;
    std::queue<QueuedTask> taskQueue;
    std::atomic<bool> done{false};
    std::atomic<int> completed{0};
    sead::Event batchDone{true}; // manual reset: cleared by run(), set when a batch ends
};

WorkerThreadMgr::WorkerThreadMgr() : m_impl(new Impl()) {}

WorkerThreadMgr::~WorkerThreadMgr() {
    if (m_impl->thread != nullptr) {
        m_impl->thread->quitAndWaitDoneSingleThread(false);
        delete m_impl->thread;
    }
    delete m_impl;
}

void WorkerThreadMgr::createWorker(void * /*sys*/, std::uint64_t /*config*/) {
    if (m_impl->thread != nullptr) return; // idempotent
    m_impl->thread = new sead::DelegateThread(
        "Lp::Sys::WorkerThread", &m_impl->delegate, /*heap=*/nullptr,
        sead::Thread::cDefaultPriority, sead::MessageQueue::BlockType::Blocking, kQuitMsg,
        /*stackSize=*/0x8000, /*messageQueueSize=*/8);
    m_impl->thread->start();
    std::printf("Lp::Sys::WorkerThreadMgr::createWorker: started sead::DelegateThread\n");
}

void WorkerThreadMgr::enqueue(WorkerTask *task, std::int64_t arg2, std::uint32_t /*flags*/) {
    if (task == nullptr || task->fn == nullptr) return;
    // Use task->arg unless the caller explicitly passed a non-zero override in arg2.
    void *arg = (arg2 != 0) ? reinterpret_cast<void *>(arg2) : task->arg;
    std::lock_guard<std::mutex> lock(m_impl->mx);
    m_impl->taskQueue.push({task->fn, arg});
}

void WorkerThreadMgr::run(std::int64_t /*workerIdx*/, bool blocking) {
    if (m_impl->thread == nullptr) {
        std::printf("Lp::Sys::WorkerThreadMgr::run: no worker (createWorker not called)\n");
        return;
    }
    m_impl->done = false;
    m_impl->batchDone.resetSignal();
    m_impl->thread->sendMessage(kRunMsg, sead::MessageQueue::BlockType::Blocking);
    if (blocking) m_impl->batchDone.wait();
}

bool WorkerThreadMgr::isDone() const { return m_impl->done.load(); }

int WorkerThreadMgr::tasksCompleted() const { return m_impl->completed.load(); }

} // namespace Lp::Sys
