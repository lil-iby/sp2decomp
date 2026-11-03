#pragma once
// Lp::Sys::WorkerThreadMgr — the game's worker-thread dispatcher.
//
// Evidence (BOOT_CALLGRAPH.md §3, ENGINE_REFERENCE.md):
//   createWorker  0x710123d4a0  132 bytes  3 call targets  19 body lines
//   enqueue       0x710123d1a8   88 bytes  2 call targets  26 body lines
//   run           0x710123d8c0   72 bytes  2 call targets  21 body lines
//
// Call sequence in RootTask:
//   createParallelInitThread  → createWorker(this, sys*, config)
//   runParallelInitThread     → enqueue(this, task*, arg2, flags)
//                             → b run(workerIdx, blocking)   [tail call]
//   waitDoneParallelInitThread → sleepCurThreadMilliSec until isDone()
//
// Task descriptor layout (deduced from RootTask::parallelInitThreadFunc blr site):
//   +0x00  void (*fn)(void*)   — function pointer, invoked with arg
//   +0x08  void *arg           — opaque argument passed to fn
//
// Implementation: one sead::DelegateThread (sead::Thread -> nn::os::CreateThread) whose
// message loop runs each batch, plus a sead::Event that run(blocking) waits on.
#include <cstdint>

namespace Lp::Sys {

// The task descriptor the game passes to enqueue().
// The decompile signature `uint *param_1` is type-erased; the underlying layout
// is {fn, arg} based on the blr site in RootTask::parallelInitThreadFunc.
struct WorkerTask {
    using Fn = void (*)(void *);
    Fn    fn;   // +0x00
    void *arg;  // +0x08
};

// Single-worker thread pool that processes one batch of tasks per run() call.
// Each run() → drain-queue → signal-done cycle matches the real game's
// one-shot parallel-init pattern.
class WorkerThreadMgr {
public:
    WorkerThreadMgr();
    ~WorkerThreadMgr();

    // 0x710123d4a0. Creates the background worker thread.
    //   sys    — Lp::Sys::Sys* providing heap context (unused in our port).
    //   config — priority / stack-size cookie (unused in our port).
    void createWorker(void *sys, std::uint64_t config);

    // 0x710123d1a8. Queues a task for the next run() batch.
    //   task  — WorkerTask descriptor; fn must be non-null.
    //   arg2  — secondary argument (may override task->arg; see note below).
    //   flags — scheduling hints (unused in our port).
    //
    // Note: the real binary passes arg2 == 0 at all observed call sites; its
    // role is not established. The port stores it alongside the task but
    // ignores it when arg2 == 0 (uses task->arg instead).
    void enqueue(WorkerTask *task, std::int64_t arg2, std::uint32_t flags);

    // 0x710123d8c0. Signals the worker thread to process queued tasks.
    //   workerIdx — which worker to wake (we have exactly one; any value fires it).
    //   blocking  — if true, blocks until all queued tasks have completed.
    //
    // The run signature recorded in Ghidra as `(int, bool)` matches this:
    // workerIdx is an int and blocking is a bool.
    void run(std::int64_t workerIdx, bool blocking);

    // Port-only diagnostic: true once the current batch has finished.
    // Used by RootTask::waitDoneParallelInitThread (which polls sleepCurThreadMilliSec)
    // and by our unit test.
    bool isDone() const;

    // Port-only: number of tasks that completed in the last run() batch.
    int tasksCompleted() const;

private:
    struct Impl;
    Impl *m_impl; // raw pointer avoids <memory> in this header
};

} // namespace Lp::Sys
