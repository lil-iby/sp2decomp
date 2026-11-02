#pragma once
// Lp::Sys::RootTask — parallel init orchestration layer.
//
// Evidence (BOOT_CALLGRAPH.md §3, ENGINE_REFERENCE.md):
//   createParallelInitThread   0x71011fcadc  104 bytes  2 call targets
//   runParallelInitThread      0x71011fcb44   96 bytes  3 call targets
//   waitDoneParallelInitThread 0x71011fcba4   56 bytes  2 call targets
//   parallelInitThreadFunc     0x71011fca28  180 bytes  4 call targets
//
// Call sequence driven by SysInit::Init after the boot setup:
//   createParallelInitThread   → WorkerThreadMgr::createWorker
//   runParallelInitThread(idx) → WorkerThreadMgr::enqueue
//                              → b WorkerThreadMgr::run   (tail call, non-blocking)
//   [Framework::run here]
//   waitDoneParallelInitThread → spin-sleeps until WorkerThreadMgr::isDone()
//
// The worker body is parallelInitThreadFunc (static, run on the worker thread).
// The actual subsystem dispatch inside it is not yet ported; the stub prints a
// placeholder and returns, which lets the orchestration tests pass cleanly.
#include "lp/worker_thread_mgr.hpp"
#include <cstdint>

namespace Lp::Sys {

class RootTask {
public:
    RootTask();

    // 0x71011fcadc. Launches the worker thread; idempotent.
    void createParallelInitThread();

    // 0x71011fcb44. Enqueues the init task and fires the worker (non-blocking).
    //   param1 — worker index forwarded verbatim to WorkerThreadMgr::run.
    void runParallelInitThread(std::uint32_t param1);

    // 0x71011fcba4. Spin-waits (via sleepCurThreadMilliSec) until the batch is done.
    void waitDoneParallelInitThread();

    // 0x71011fca28. The task body executed on the worker thread.
    //   arg is always `this` (the owning RootTask*); subsystem dispatch is not yet ported.
    static void parallelInitThreadFunc(void *arg);

private:
    WorkerThreadMgr m_workerMgr;
    WorkerTask      m_initTask;
};

} // namespace Lp::Sys
