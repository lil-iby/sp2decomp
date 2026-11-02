// sead::CriticalSection / ReadWriteLock over host synchronisation primitives.
#include "sead/lock.hpp"

#include <mutex>
#include <new>
#include <shared_mutex>

namespace sead {

namespace {
// Placement-manage the host primitives inside the opaque storage the header reserves, so the
// header needs no <mutex> and the object layout is fixed regardless of platform.
template <typename T, std::size_t N>
T *As(unsigned char (&storage)[N]) {
    static_assert(sizeof(T) <= N, "storage in sead/lock.hpp is too small for the host type");
    return reinterpret_cast<T *>(storage);
}
} // namespace

CriticalSection::CriticalSection() {
    new (mStorage) std::recursive_mutex();
    mInitialised = true;
}

CriticalSection::~CriticalSection() {
    if (mInitialised) {
        As<std::recursive_mutex>(mStorage)->~recursive_mutex();
        mInitialised = false;
    }
}

void CriticalSection::lock() { As<std::recursive_mutex>(mStorage)->lock(); }
void CriticalSection::unlock() { As<std::recursive_mutex>(mStorage)->unlock(); }
bool CriticalSection::tryLock() { return As<std::recursive_mutex>(mStorage)->try_lock(); }

ReadWriteLock::ReadWriteLock() {
    new (mStorage) std::shared_mutex();
    mInitialised = true;
}

ReadWriteLock::~ReadWriteLock() {
    if (mInitialised) {
        As<std::shared_mutex>(mStorage)->~shared_mutex();
        mInitialised = false;
    }
}

void ReadWriteLock::lockRead() { As<std::shared_mutex>(mStorage)->lock_shared(); }
void ReadWriteLock::lockWrite() { As<std::shared_mutex>(mStorage)->lock(); }
void ReadWriteLock::unlockRead() { As<std::shared_mutex>(mStorage)->unlock_shared(); }
void ReadWriteLock::unlockWrite() { As<std::shared_mutex>(mStorage)->unlock(); }

} // namespace sead
