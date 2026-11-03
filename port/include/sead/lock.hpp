// sead::CriticalSection and sead::ReadWriteLock — the locks Lp::Sys::HeapGroup owns.
//
// HeapGroup::createInstance (0x71011f4ffc, 160 bytes) constructs exactly these two at fixed
// offsets inside its 0x160-byte singleton:
//     sead::CriticalSection::CriticalSection((CriticalSection *)(obj + 2));   // +0x10
//     sead::ReadWriteLock::ReadWriteLock((ReadWriteLock *)(obj + 10));        // +0x50
//
// CriticalSection follows the binary: an IDisposer holding a recursive nn::os mutex
// (ctors 0x7100e98ab0 60B / 0x7100e98aec 64B, lock 0x7100e98bd8, unlock 0x7100e98be8, each
// one nn::os call). ReadWriteLock is built from two sead::Semaphores. Behaviour of both
// from open-ead/sead, written independently.
#pragma once

#include "nn/os.hpp"
#include "sead/i_disposer.hpp"
#include "sead/semaphore.hpp"

#include <atomic>
#include <cstdint>

namespace sead {

class Thread;

class CriticalSection : public IDisposer {
public:
    CriticalSection();
    explicit CriticalSection(Heap *disposerHeap);
    CriticalSection(Heap *disposerHeap, HeapNullOption option);
    ~CriticalSection() override;

    void lock();
    void unlock();
    bool tryLock();
    bool try_lock() { return tryLock(); } // standard Lockable spelling

    CriticalSection(const CriticalSection &) = delete;
    CriticalSection &operator=(const CriticalSection &) = delete;

    nn::os::MutexType mCriticalSectionInner{};
};

// HeapGroup puts a CriticalSection at +0x10 and a ReadWriteLock at +0x50.
static_assert(sizeof(CriticalSection) == 0x40, "sead::CriticalSection must be 0x40 bytes");

// sead::ReadWriteLock (ctor 0x7100e998a8, 84B: the two semaphores' constructors).
// Readers share, a writer excludes everyone. The first reader takes the write lock; the
// writer is identified by ThreadMgr::getCurrentThread(), which makes writeLock re-entrant
// for that thread. Consequence kept from sead: a thread that is not a sead::Thread
// (getCurrentThread() == null) does not actually take the write lock.
class ReadWriteLock {
public:
    ReadWriteLock() = default;
    ~ReadWriteLock() = default;

    void readLock();
    void readUnlock();
    void writeLock();
    void writeUnlock();

    ReadWriteLock(const ReadWriteLock &) = delete;
    ReadWriteLock &operator=(const ReadWriteLock &) = delete;

private:
    // A mutex that only touches the semaphore when contended (count > 1).
    class SemaphoreLock {
    public:
        void lock();
        void unlock();

    private:
        Semaphore mSemaphore{0, 0x7FFF};
        std::atomic<std::uint32_t> mLockCount{0};
    };

    std::atomic<std::uint32_t> mNumReaders{0};
    std::atomic<std::uint32_t> mNumWriters{0};
    SemaphoreLock mReadLock;
    SemaphoreLock mWriteLock;
    Thread *mWriterThread = nullptr;
    std::uint32_t mWritingThreadCount = 0;
};

// HeapGroup reserves +0x50 .. +0x108 for it.
static_assert(sizeof(ReadWriteLock) == 0xB8, "sead::ReadWriteLock must be 0xB8 bytes");

} // namespace sead
