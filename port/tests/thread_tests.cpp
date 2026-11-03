// thread_tests.cpp — sead::MessageQueue, sead::Thread and the nn::os layers under them.
#include "sead/heap.hpp"
#include "sead/heap_mgr.hpp"
#include "sead/message_queue.hpp"
#include "sead/thread.hpp"
#include "sead/thread_mgr.hpp"
#include "nn/os.hpp"

#include <atomic>
#include <cstdio>
#include <string>

namespace {
int g_failures = 0, g_checks = 0;

void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else        std::printf("  ok    %s\n", what);
}

using BT = sead::MessageQueue::BlockType;
constexpr sead::MessageQueue::Element kQuit = 0x7FFFFFFF;

// Sums every message it receives and records which sead::Thread ran its body.
class SumThread : public sead::Thread {
public:
    explicit SumThread(sead::Heap *heap = nullptr)
        : Thread("SumThread", heap, cDefaultPriority, BT::Blocking, kQuit, 0x4000, 8) {}

    std::atomic<long long> sum{0};
    std::atomic<int> calls{0};
    std::atomic<sead::Thread *> seenCurrent{nullptr};
    std::atomic<std::uint64_t> seenOsId{0};

protected:
    void calc_(sead::MessageQueue::Element msg) override {
        sum += msg;
        ++calls;
        seenCurrent = sead::ThreadMgr::instance()->getCurrentThread();
        seenOsId = nn::os::GetThreadId(nn::os::GetCurrentThread());
    }
};
} // namespace

static void test_message_queue_basics() {
    std::printf("\n[1] MessageQueue FIFO, jam, peek, bounds\n");
    sead::MessageQueue q;
    q.allocate(3, nullptr);
    Check(q.pop(BT::NonBlocking) == sead::MessageQueue::cNullElement, "empty non-blocking pop -> 0");
    Check(q.push(1, BT::NonBlocking) && q.push(2, BT::NonBlocking), "push 1, 2");
    Check(q.jam(9, BT::NonBlocking), "jam 9 to the front");
    Check(!q.push(4, BT::NonBlocking), "push on a full queue fails without blocking");
    Check(q.peek(BT::NonBlocking) == 9, "peek sees the jammed message");
    Check(q.pop(BT::Blocking) == 9, "pop 9 (peek did not remove it)");
    Check(q.pop(BT::Blocking) == 1, "pop 1");
    Check(q.pop(BT::Blocking) == 2, "pop 2");
    Check(q.pop(BT::NonBlocking) == sead::MessageQueue::cNullElement, "empty again");
    q.free();
}

static void test_message_queue_heap() {
    std::printf("\n[2] MessageQueue buffer comes from and returns to the heap\n");
    sead::HeapMgr::initialize(1024 * 1024);
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    const int before = root->liveAllocationCount();
    {
        sead::MessageQueue q;
        q.allocate(16, root);
        Check(root->liveAllocationCount() == before + 1, "one allocation for the buffer");
        q.free();
        Check(root->liveAllocationCount() == before, "buffer returned on free()");
    }
    sead::HeapMgr::destroy();
}

static void test_thread_runs_messages() {
    std::printf("\n[3] Thread processes messages until quit\n");
    const std::size_t baseCount = sead::ThreadMgr::instance()->threadCount();
    {
        SumThread t;
        Check(sead::ThreadMgr::instance()->threadCount() == baseCount + 1, "registered with ThreadMgr");
        Check(t.getState() == sead::Thread::State::cInitialized, "starts Initialized");
        Check(t.start(), "start() succeeds");
        Check(!t.start(), "second start() is refused");
        t.sendMessage(1, BT::Blocking);
        t.sendMessage(2, BT::Blocking);
        t.sendMessage(3, BT::Blocking);
        t.quitAndWaitDoneSingleThread(false);
        Check(t.sum.load() == 6, "all three messages reached calc_");
        Check(t.getState() == sead::Thread::State::cReleased, "Released after waitDone");
        Check(t.isDone(), "isDone()");
        Check(t.seenCurrent.load() == &t, "getCurrentThread() inside calc_ is the thread");
        Check(t.seenOsId.load() != 0 && t.seenOsId.load() == t.getId(),
              "nn::os::GetCurrentThread() inside calc_ is the thread's own ThreadType");
    }
    Check(sead::ThreadMgr::instance()->threadCount() == baseCount, "unregistered on destruction");
    Check(sead::ThreadMgr::instance()->getCurrentThread() == nullptr,
          "getCurrentThread() on the test's own thread is null");
}

static void test_quit_jam_skips_pending() {
    std::printf("\n[4] quit(jam) jumps the queue, quit(push) waits its turn\n");
    {
        SumThread t;
        t.sendMessage(1, BT::Blocking);
        t.sendMessage(2, BT::Blocking);
        t.quit(true);
        t.start();
        t.waitDone();
        Check(t.calls.load() == 0, "jammed quit ran before the pending messages");
    }
    {
        SumThread t;
        t.sendMessage(1, BT::Blocking);
        t.sendMessage(2, BT::Blocking);
        t.quit(false);
        t.start();
        t.waitDone();
        Check(t.sum.load() == 3, "pushed quit ran after the pending messages");
    }
}

static void test_send_rejections() {
    std::printf("\n[5] sendMessage rejects the null element and the quit message\n");
    SumThread t;
    Check(!t.sendMessage(sead::MessageQueue::cNullElement, BT::NonBlocking), "null element rejected");
    Check(!t.sendMessage(kQuit, BT::NonBlocking), "quit message rejected (use quit())");
    t.start();
    t.quitAndWaitDoneSingleThread(false);
    Check(!t.sendMessage(5, BT::NonBlocking), "rejected once the thread is done");
}

static void test_sleep() {
    std::printf("\n[6] Thread::sleep waits at least the requested time\n");
    const std::uint64_t start = nn::os::GetSystemTick();
    sead::Thread::sleep(sead::TickSpan::fromMilliSeconds(10));
    const std::int64_t elapsedNs =
        nn::os::ConvertToTimeSpan(static_cast<std::int64_t>(nn::os::GetSystemTick() - start));
    Check(elapsedNs >= 9'000'000, "slept for at least ~10 ms");
}

// Must run last: it gives the test thread a MainThread, which the other tests assume absent.
static void test_main_thread() {
    std::printf("\n[7] ThreadMgr::initialize creates the MainThread\n");
    sead::HeapMgr::initialize(1024 * 1024);
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    sead::ExpHeap *heap = sead::ExpHeap::create(0, "sead::ThreadMgr", root);
    sead::ThreadMgr *mgr = sead::ThreadMgr::instance();
    const std::size_t listBefore = mgr->threadCount();
    const int allocsBefore = heap->liveAllocationCount();

    sead::ThreadMgr::initialize(mgr, heap);
    sead::Thread *mainThread = mgr->getMainThread();
    Check(mainThread != nullptr, "getMainThread() is set");
    if (mainThread == nullptr) return;
    Check(heap->liveAllocationCount() > allocsBefore, "the MainThread lives in the given heap");
    Check(mgr->getCurrentThread() == mainThread, "the calling thread is now the main thread");
    Check(mgr->isMainThread(), "isMainThread() on the calling thread");
    Check(std::string(mainThread->getName()) == "sead::MainThread", "named sead::MainThread");
    Check(mainThread->getState() == sead::Thread::State::cRunning, "already Running");
    Check(mainThread->getId() == nn::os::GetThreadId(nn::os::GetCurrentThread()),
          "its id is the calling thread's nn::os id");
    Check(mainThread->getPriority() == sead::Thread::cDefaultPriority,
          "initMainThread_ set the calling thread's priority to 16");
    Check(mainThread->getBlockType() == BT::NonBlocking, "non-blocking message queue");
    Check(mgr->threadCount() == listBefore, "not counted among created threads");

    {
        SumThread worker;
        worker.start();
        worker.sendMessage(1, BT::Blocking);
        worker.quitAndWaitDoneSingleThread(false);
        Check(worker.seenCurrent.load() == &worker, "a worker still sees itself, not the main thread");
    }

    mainThread->quit(false); // refused for the main thread
    Check(mainThread->getState() == sead::Thread::State::cRunning, "quit() does not stop the main thread");

    sead::ThreadMgr::initialize(mgr, heap); // re-initialising replaces it
    Check(mgr->getMainThread() != nullptr, "re-initialise builds a new MainThread");

    mgr->destroy();
    Check(mgr->getMainThread() == nullptr, "destroy() removes the MainThread");
    Check(mgr->getCurrentThread() == nullptr, "and clears the current-thread slot");
    Check(heap->liveAllocationCount() == allocsBefore, "its memory is returned to the heap");

    sead::HeapMgr::destroy();
}

int main() {
    std::printf("sead::Thread / MessageQueue tests\n");
    test_message_queue_basics();
    test_message_queue_heap();
    test_thread_runs_messages();
    test_quit_jam_skips_pending();
    test_send_rejections();
    test_sleep();
    test_main_thread();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
