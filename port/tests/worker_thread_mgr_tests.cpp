// worker_thread_mgr_tests.cpp — contract tests for Lp::Sys::WorkerThreadMgr.
//
// Verifies the three-function dispatch sequence the boot path exercises:
//   createWorker → enqueue → run(blocking=true) → task executed on worker thread
//
// Each test is independent and creates a fresh WorkerThreadMgr.
#include "lp/worker_thread_mgr.hpp"
#include "sead/thread.hpp"
#include "sead/thread_mgr.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------
namespace {
int g_failures = 0, g_checks = 0;

void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else        std::printf("  ok    %s\n", what);
}
} // namespace

// ---------------------------------------------------------------------------
// Task helpers
// ---------------------------------------------------------------------------
static void incrementCounter(void *arg) {
    auto *counter = static_cast<std::atomic<int> *>(arg);
    ++(*counter);
}

static void appendToAtomic(void *arg) {
    // Adds 10 to the counter — distinguishable from increment above.
    auto *counter = static_cast<std::atomic<int> *>(arg);
    counter->fetch_add(10);
}

// ---------------------------------------------------------------------------
// Test 1 — basic createWorker / enqueue / run(blocking)
// ---------------------------------------------------------------------------
static void test_basic_dispatch() {
    std::printf("\n[1] createWorker → enqueue → run(blocking=true)\n");

    Lp::Sys::WorkerThreadMgr mgr;
    std::atomic<int> counter{0};

    mgr.createWorker(nullptr, 0);

    Lp::Sys::WorkerTask task{incrementCounter, &counter};
    mgr.enqueue(&task, 0, 0);

    mgr.run(0, true); // blocking — must not return until task runs

    Check(counter.load() == 1,    "task executed exactly once after blocking run()");
    Check(mgr.isDone(),            "isDone() is true after blocking run()");
    Check(mgr.tasksCompleted() == 1, "tasksCompleted() == 1");
}

// ---------------------------------------------------------------------------
// Test 2 — multiple tasks in one batch
// ---------------------------------------------------------------------------
static void test_multi_task_batch() {
    std::printf("\n[2] enqueue 3 tasks, run once, all complete\n");

    Lp::Sys::WorkerThreadMgr mgr;
    std::atomic<int> counter{0};
    mgr.createWorker(nullptr, 0);

    Lp::Sys::WorkerTask t{incrementCounter, &counter};
    mgr.enqueue(&t, 0, 0);
    mgr.enqueue(&t, 0, 0);
    mgr.enqueue(&t, 0, 0);

    mgr.run(0, true);

    Check(counter.load() == 3,    "all 3 tasks ran after single run()");
    Check(mgr.tasksCompleted() == 3, "tasksCompleted() == 3");
}

// ---------------------------------------------------------------------------
// Test 3 — two sequential batches (simulates init wave 1 then wave 2)
// ---------------------------------------------------------------------------
static void test_sequential_batches() {
    std::printf("\n[3] two sequential enqueue+run batches\n");

    Lp::Sys::WorkerThreadMgr mgr;
    std::atomic<int> counter{0};
    mgr.createWorker(nullptr, 0);

    // Batch 1: increment once
    Lp::Sys::WorkerTask t1{incrementCounter, &counter};
    mgr.enqueue(&t1, 0, 0);
    mgr.run(0, true);
    Check(counter.load() == 1, "batch 1: counter == 1");

    // Batch 2: add 10
    Lp::Sys::WorkerTask t2{appendToAtomic, &counter};
    mgr.enqueue(&t2, 0, 0);
    mgr.run(0, true);
    Check(counter.load() == 11, "batch 2: counter == 11");
    Check(mgr.tasksCompleted() == 1, "batch 2: tasksCompleted() == 1 (second batch only)");
}

// ---------------------------------------------------------------------------
// Test 4 — task runs on a DIFFERENT thread
// ---------------------------------------------------------------------------
static void test_runs_on_worker_thread() {
    std::printf("\n[4] task executes on a different thread than the caller\n");

    Lp::Sys::WorkerThreadMgr mgr;
    const auto callerThreadId = std::this_thread::get_id();
    std::atomic<bool> ranOnDifferentThread{false};

    struct Ctx {
        std::thread::id     callerThreadId;
        std::atomic<bool>  *result;
    } ctx{callerThreadId, &ranOnDifferentThread};

    auto taskFn = [](void *arg) {
        auto *c = static_cast<Ctx *>(arg);
        if (std::this_thread::get_id() != c->callerThreadId)
            c->result->store(true);
    };

    mgr.createWorker(nullptr, 0);
    Lp::Sys::WorkerTask task{taskFn, &ctx};
    mgr.enqueue(&task, 0, 0);
    mgr.run(0, true);

    Check(ranOnDifferentThread.load(),
          "task ran on the worker thread, not the calling thread");
}

// ---------------------------------------------------------------------------
// Test 5 — isDone() is false before run()
// ---------------------------------------------------------------------------
static void test_not_done_before_run() {
    std::printf("\n[5] isDone() is false before any run()\n");

    Lp::Sys::WorkerThreadMgr mgr;
    mgr.createWorker(nullptr, 0);

    Check(!mgr.isDone(), "isDone() is false before run()");
}

// ---------------------------------------------------------------------------
// Test 6 — the worker is a sead::Thread registered with sead::ThreadMgr
// ---------------------------------------------------------------------------
static void test_worker_is_sead_thread() {
    std::printf("\n[6] tasks run inside a sead::Thread\n");

    const std::size_t before = sead::ThreadMgr::instance()->threadCount();
    std::atomic<sead::Thread *> seen{nullptr};
    {
        Lp::Sys::WorkerThreadMgr mgr;
        mgr.createWorker(nullptr, 0);
        Check(sead::ThreadMgr::instance()->threadCount() == before + 1,
              "createWorker registers one sead::Thread");

        Lp::Sys::WorkerTask task{
            [](void *arg) {
                static_cast<std::atomic<sead::Thread *> *>(arg)->store(
                    sead::ThreadMgr::instance()->getCurrentThread());
            },
            &seen};
        mgr.enqueue(&task, 0, 0);
        mgr.run(0, true);
        Check(seen.load() != nullptr, "getCurrentThread() inside the task is the worker");
    }
    Check(sead::ThreadMgr::instance()->threadCount() == before,
          "destroying the manager quits and unregisters the worker");
}

// ---------------------------------------------------------------------------
// Test 7 — run() without a worker returns instead of hanging
// ---------------------------------------------------------------------------
static void test_run_without_worker() {
    std::printf("\n[7] run(blocking) without createWorker\n");
    Lp::Sys::WorkerThreadMgr mgr;
    mgr.run(0, true);
    Check(!mgr.isDone(), "returned, and no batch is reported done");
}

// ---------------------------------------------------------------------------
int main() {
    std::printf("Lp::Sys::WorkerThreadMgr tests\n");
    std::printf("(3-function dispatch: createWorker / enqueue / run)\n");

    test_basic_dispatch();
    test_multi_task_batch();
    test_sequential_batches();
    test_runs_on_worker_thread();
    test_not_done_before_run();
    test_worker_is_sead_thread();
    test_run_without_worker();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
