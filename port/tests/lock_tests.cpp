// lock_tests.cpp — sead::Semaphore, sead::CriticalSection layout, sead::ReadWriteLock.
#include "sead/heap.hpp"
#include "sead/heap_mgr.hpp"
#include "sead/lock.hpp"
#include "sead/semaphore.hpp"
#include "sead/thread.hpp"
#include "sead/thread_mgr.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {
int g_failures = 0, g_checks = 0;

void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else        std::printf("  ok    %s\n", what);
}

void Pause() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }

using BT = sead::MessageQueue::BlockType;

// A sead::Thread that takes the write lock for each message it receives.
class WriterThread : public sead::Thread {
public:
    explicit WriterThread(sead::ReadWriteLock &rw)
        : Thread("WriterThread", nullptr, cDefaultPriority, BT::Blocking, 0x7FFFFFFF, 0x4000, 4),
          mRw(rw) {}
    std::atomic<bool> wrote{false};

protected:
    void calc_(sead::MessageQueue::Element) override {
        mRw.writeLock();
        wrote = true;
        mRw.writeUnlock();
    }

private:
    sead::ReadWriteLock &mRw;
};
} // namespace

static void test_sizes() {
    std::printf("\n[1] layouts HeapGroup depends on\n");
    Check(sizeof(sead::CriticalSection) == 0x40, "CriticalSection 0x40");
    Check(sizeof(sead::Semaphore) == 0x48, "Semaphore 0x48 (IDisposer + SemaphoreType)");
    Check(sizeof(sead::ReadWriteLock) == 0xB8, "ReadWriteLock 0xB8");
}

static void test_semaphore() {
    std::printf("\n[2] sead::Semaphore\n");
    sead::Semaphore one(1, 1);
    Check(one.tryLock(), "count 1: tryLock succeeds");
    Check(!one.tryLock(), "count 0: tryLock fails");
    one.unlock();
    Check(one.tryLock(), "released: tryLock succeeds again");
    one.unlock();

    sead::Semaphore zero(0, 1);
    std::atomic<bool> acquired{false};
    std::thread waiter([&] { zero.lock(); acquired = true; });
    Pause();
    Check(!acquired.load(), "lock() on count 0 blocks");
    zero.unlock();
    waiter.join();
    Check(acquired.load(), "unlock() from another thread wakes it");
}

static void test_readers_share() {
    std::printf("\n[3] readers share the lock\n");
    sead::ReadWriteLock rw;
    rw.readLock();
    std::atomic<bool> otherRead{false};
    std::thread other([&] { rw.readLock(); otherRead = true; rw.readUnlock(); });
    other.join(); // would hang if a second reader were excluded
    Check(otherRead.load(), "a second reader gets in while the first holds it");
    rw.readUnlock();
}

static void test_writer_excludes_reader() {
    std::printf("\n[4] a writer excludes readers (and re-enters)\n");
    sead::HeapMgr::initialize(1024 * 1024);
    sead::ExpHeap *heap = sead::ExpHeap::create(0, "sead::ThreadMgr", sead::HeapMgr::getRootHeap(0));
    sead::ThreadMgr::initialize(sead::ThreadMgr::instance(), heap); // this thread becomes a sead::Thread

    sead::ReadWriteLock rw;
    rw.writeLock();
    rw.writeLock(); // same writer: re-entrant, must not deadlock
    std::atomic<bool> readerIn{false};
    std::thread reader([&] { rw.readLock(); readerIn = true; rw.readUnlock(); });
    Pause();
    Check(!readerIn.load(), "a reader waits while the write lock is held");
    rw.writeUnlock();
    Pause();
    Check(!readerIn.load(), "still waits after the first of two nested unlocks");
    rw.writeUnlock();
    reader.join();
    Check(readerIn.load(), "gets in once the writer fully releases");

    sead::ThreadMgr::instance()->destroy();
    sead::HeapMgr::destroy();
}

static void test_reader_excludes_writer() {
    std::printf("\n[5] a reader excludes a writer running in a sead::Thread\n");
    sead::ReadWriteLock rw;
    WriterThread writer(rw);
    writer.start();

    rw.readLock();
    writer.sendMessage(1, BT::Blocking);
    Pause();
    Check(!writer.wrote.load(), "the writer waits while a reader holds the lock");
    rw.readUnlock();
    writer.quitAndWaitDoneSingleThread(false);
    Check(writer.wrote.load(), "and writes once the reader leaves");
}

int main() {
    std::printf("sead lock tests\n");
    test_sizes();
    test_semaphore();
    test_readers_share();
    test_writer_excludes_reader();
    test_reader_excludes_writer();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
