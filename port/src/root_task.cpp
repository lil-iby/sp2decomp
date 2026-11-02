// Lp::Sys::RootTask — port of the parallel-init orchestration methods.
//
// Semantics from BOOT_CALLGRAPH.md and ENGINE_REFERENCE.md:
//   createParallelInitThread  → createWorker, sets up the WorkerTask descriptor
//   runParallelInitThread     → enqueue + run (non-blocking, tail-call in binary)
//   waitDoneParallelInitThread → spin with sleepCurThreadMilliSec until isDone()
//   parallelInitThreadFunc    → the task body (subsystem dispatch stub for now)
//
// The real binary uses nn::os::CreateThread directly; here WorkerThreadMgr
// already wraps std::thread, so RootTask sits cleanly on top of it.
#include "lp/root_task.hpp"
#include "lp/utl.hpp"

#include <cstdio>

namespace Lp::Sys {

RootTask::RootTask()
    : m_workerMgr()
    , m_initTask{parallelInitThreadFunc, this}
{}

void RootTask::createParallelInitThread() {
    // Pass `this` as the Sys* context and 0 as the config cookie.
    // The binary passes (sys, config) from SysInit; both are opaque to our port.
    m_workerMgr.createWorker(this, 0);
    std::printf("Lp::Sys::RootTask::createParallelInitThread: worker ready\n");
}

void RootTask::runParallelInitThread(std::uint32_t param1) {
    // The binary's body is:  enqueue(...); b run(workerIdx, blocking)
    // `b` (not `bl`) is a tail call, so the function returns when run() returns.
    // The real `blocking` flag is deduced as false: SysInit calls
    // waitDoneParallelInitThread separately, implying run() here is non-blocking.
    m_workerMgr.enqueue(&m_initTask, 0, 0);
    m_workerMgr.run(static_cast<std::int64_t>(param1), /*blocking=*/false);
    std::printf("Lp::Sys::RootTask::runParallelInitThread: dispatched (worker idx=%u)\n",
                param1);
}

void RootTask::waitDoneParallelInitThread() {
    // The binary spin-loops calling sleepCurThreadMilliSec until the worker is done.
    // 1 ms sleep matches the observed call pattern (small constant argument).
    while (!m_workerMgr.isDone()) {
        Lp::Utl::sleepCurThreadMilliSec(1);
    }
    std::printf("Lp::Sys::RootTask::waitDoneParallelInitThread: parallel init complete\n");
}

void RootTask::parallelInitThreadFunc(void *arg) {
    // arg == owning RootTask*.
    // The real body dispatches subsystem initialization tasks via blr <indirect>.
    // Those subsystems are not yet ported; the placeholder keeps the test harness passing.
    (void)arg;
    std::printf("Lp::Sys::RootTask::parallelInitThreadFunc: parallel init placeholder\n");
}

} // namespace Lp::Sys
