// sead::CriticalSection and sead::ReadWriteLock — the locks Lp::Sys::HeapGroup owns.
//
// HeapGroup::createInstance (0x71011f4ffc, 160 bytes) constructs exactly these two at fixed
// offsets inside its 0x160-byte singleton:
//     sead::CriticalSection::CriticalSection((CriticalSection *)(obj + 2));   // +0x10
//     sead::ReadWriteLock::ReadWriteLock((ReadWriteLock *)(obj + 10));        // +0x50
//
// CriticalSection follows the binary: an IDisposer holding a recursive nn::os mutex
// (ctors 0x7100e98ab0 60B / 0x7100e98aec 64B, lock 0x7100e98bd8, unlock 0x7100e98be8, each
// one nn::os call; behaviour from open-ead/sead, written independently). ReadWriteLock is
// still a host shared_mutex behind reserved storage.
#pragma once

#include "nn/os.hpp"
#include "sead/i_disposer.hpp"

namespace sead {

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

class ReadWriteLock {
public:
    ReadWriteLock();
    ~ReadWriteLock();

    void lockRead();
    void lockWrite();
    void unlockRead();
    void unlockWrite();

    ReadWriteLock(const ReadWriteLock &) = delete;
    ReadWriteLock &operator=(const ReadWriteLock &) = delete;

private:
    alignas(8) unsigned char mStorage[0xB0] = {}; // total 0xB8 so HeapGroup lands mDisposer at +0x108
    bool mInitialised = false;
};

} // namespace sead
