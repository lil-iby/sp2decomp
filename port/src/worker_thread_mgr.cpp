// Lp::Sys::WorkerThreadMgr — backed by std::thread + std::queue.
//
// Semantics modelled from BOOT_CALLGRAPH.md:
//   createWorker → launch idle background thread (waits for run())
//   enqueue      → push task onto queue
//   run          → signal "go", optionally block until queue is drained
//
// The real binary uses nn::os::CreateThread + nn::os::MessageQueue;
// we inline those semantics here because nn::os::CreateThread has not
// yet been declared in os.hpp.
#include "lp/worker_thread_mgr.hpp"

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <queue>
#include <thread>

namespace Lp::Sys {

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------
struct WorkerThreadMgr::Impl {
    struct QueuedTask {
        WorkerTask::Fn fn;
        void          *arg;
    };

    std::thread             worker;
    std::queue<QueuedTask>  taskQueue;
    std::mutex              mx;
    std::condition_variable readyToRun;  // worker waits here until run() fires
    std::condition_variable batchDone;   // run(blocking=true) waits here

    bool goFlag  = false; // set by run(), cleared by worker after draining queue
    bool exitFlag = false; // set by destructor
    int  completed = 0;  // tasks finished in the last batch
    bool done = false;   // true once the last batch finished
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

WorkerThreadMgr::WorkerThreadMgr() : m_impl(new Impl{}) {}

WorkerThreadMgr::~WorkerThreadMgr() {
    {
        std::lock_guard<std::mutex> lock(m_impl->mx);
        m_impl->exitFlag = true;
        m_impl->goFlag = true; // wake thread so it can see exitFlag
    }
    m_impl->readyToRun.notify_all();
    if (m_impl->worker.joinable()) m_impl->worker.join();
    delete m_impl;
}

void WorkerThreadMgr::createWorker(void * /*sys*/, std::uint64_t /*config*/) {
    if (m_impl->worker.joinable()) return; // idempotent

    // Inline the worker loop as a lambda so it is a member-function context —
    // this avoids the private-Impl access issue that a free `workerLoop` function
    // would hit when naming WorkerThreadMgr::Impl from outside the class.
    Impl *impl = m_impl;
    m_impl->worker = std::thread([impl] {
        std::unique_lock<std::mutex> lock(impl->mx);
        while (!impl->exitFlag) {
            impl->readyToRun.wait(lock, [impl] {
                return impl->goFlag || impl->exitFlag;
            });
            if (impl->exitFlag) break;

            impl->done = false;
            impl->completed = 0;
            while (!impl->taskQueue.empty()) {
                auto task = impl->taskQueue.front();
                impl->taskQueue.pop();
                lock.unlock();
                task.fn(task.arg);
                lock.lock();
                ++impl->completed;
            }
            impl->goFlag = false;
            impl->done = true;
            impl->batchDone.notify_all();
        }
    });
    std::printf("Lp::Sys::WorkerThreadMgr::createWorker: launched background thread\n");
}

void WorkerThreadMgr::enqueue(WorkerTask *task, std::int64_t arg2, std::uint32_t /*flags*/) {
    if (task == nullptr || task->fn == nullptr) return;
    // Use task->arg unless the caller explicitly passed a non-zero override in arg2.
    void *arg = (arg2 != 0) ? reinterpret_cast<void *>(arg2) : task->arg;
    std::lock_guard<std::mutex> lock(m_impl->mx);
    m_impl->taskQueue.push({task->fn, arg});
    std::printf("Lp::Sys::WorkerThreadMgr::enqueue: queued task fn=%p arg=%p\n",
                reinterpret_cast<void *>(task->fn), arg);
}

void WorkerThreadMgr::run(std::int64_t /*workerIdx*/, bool blocking) {
    {
        std::lock_guard<std::mutex> lock(m_impl->mx);
        m_impl->done = false;
        m_impl->goFlag = true;
    }
    m_impl->readyToRun.notify_all();

    if (blocking) {
        std::unique_lock<std::mutex> lock(m_impl->mx);
        m_impl->batchDone.wait(lock, [this] { return m_impl->done; });
    }
    std::printf("Lp::Sys::WorkerThreadMgr::run: %s\n",
                blocking ? "waited for batch" : "fired async");
}

bool WorkerThreadMgr::isDone() const {
    std::lock_guard<std::mutex> lock(m_impl->mx);
    return m_impl->done;
}

int WorkerThreadMgr::tasksCompleted() const {
    std::lock_guard<std::mutex> lock(m_impl->mx);
    return m_impl->completed;
}

} // namespace Lp::Sys
