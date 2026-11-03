// sead::CriticalSection / sead::ReadWriteLock — see port/include/sead/lock.hpp.
#include "sead/lock.hpp"
#include "sead/thread_mgr.hpp"

namespace sead {

CriticalSection::CriticalSection() : IDisposer() {
    nn::os::InitializeMutex(&mCriticalSectionInner, true, 0);
}

CriticalSection::CriticalSection(Heap *disposerHeap)
    : IDisposer(disposerHeap, HeapNullOption::UseSpecifiedOrContainHeap) {
    nn::os::InitializeMutex(&mCriticalSectionInner, true, 0);
}

CriticalSection::CriticalSection(Heap *disposerHeap, HeapNullOption option)
    : IDisposer(disposerHeap, option) {
    nn::os::InitializeMutex(&mCriticalSectionInner, true, 0);
}

CriticalSection::~CriticalSection() { nn::os::FinalizeMutex(&mCriticalSectionInner); }

void CriticalSection::lock() { nn::os::LockMutex(&mCriticalSectionInner); }
void CriticalSection::unlock() { nn::os::UnlockMutex(&mCriticalSectionInner); }
bool CriticalSection::tryLock() { return nn::os::TryLockMutex(&mCriticalSectionInner); }

// --- ReadWriteLock --------------------------------------------------------------------------
// fetch_add/fetch_sub return the previous count, which is what each test below reads.
// The semaphores are released by whichever thread drops the count, not necessarily the one
// that took it (the last reader out releases the write lock the first reader took), which
// is why these are semaphores rather than mutexes.

void ReadWriteLock::SemaphoreLock::lock() {
    if (mLockCount.fetch_add(1) != 0) mSemaphore.lock();
}

void ReadWriteLock::SemaphoreLock::unlock() {
    if (mLockCount.fetch_sub(1) != 1) mSemaphore.unlock();
}

void ReadWriteLock::readLock() {
    mReadLock.lock();
    if (mNumReaders.fetch_add(1) == 0) mWriteLock.lock(); // first reader shuts writers out
    mReadLock.unlock();
}

void ReadWriteLock::readUnlock() {
    if (mNumReaders.fetch_sub(1) == 1) mWriteLock.unlock(); // last reader lets them in
}

void ReadWriteLock::writeLock() {
    Thread *current = ThreadMgr::instance()->getCurrentThread();
    if (mWriterThread != current) {
        if (mNumWriters.fetch_add(1) == 0) mReadLock.lock(); // stop new readers
        mWriteLock.lock();
        mWriterThread = current;
    }
    ++mWritingThreadCount;
}

void ReadWriteLock::writeUnlock() {
    Thread *current = ThreadMgr::instance()->getCurrentThread();
    // sead asserts in both cases: not the owner, or no sead::Thread / nothing held.
    if (mWriterThread != current) return;
    if (current == nullptr || mWritingThreadCount == 0) return;

    if (--mWritingThreadCount == 0) {
        mWriterThread = nullptr;
        mWriteLock.unlock();
        if (mNumWriters.fetch_sub(1) == 1) mReadLock.unlock();
    }
}

} // namespace sead
