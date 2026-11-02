// sead::CriticalSection / ReadWriteLock over host synchronisation primitives.
#include "sead/lock.hpp"

#include <mutex>
#include <new>
#include <shared_mutex>

namespace sead {

namespace {
// The host mutex types are heap-allocated; storage holds only the pointer. This removes
// any constraint on sizeof(std::recursive_mutex) / sizeof(std::shared_mutex) — those sizes
// vary by platform (MSVC x64: ~80 bytes; GCC x64: ~40 bytes) and would require awkward
// #if guards if embedded directly. A pointer is always 8 bytes, well within the reserves.
template <typename T, std::size_t N>
T **AsPtr(unsigned char (&storage)[N]) {
    static_assert(sizeof(T *) <= N, "storage in sead/lock.hpp is too small for a pointer");
    return reinterpret_cast<T **>(storage);
}
} // namespace

CriticalSection::CriticalSection() {
    *AsPtr<std::recursive_mutex>(mStorage) = new std::recursive_mutex();
    mInitialised = true;
}

CriticalSection::~CriticalSection() {
    if (mInitialised) {
        delete *AsPtr<std::recursive_mutex>(mStorage);
        mInitialised = false;
    }
}

void CriticalSection::lock() { (*AsPtr<std::recursive_mutex>(mStorage))->lock(); }
void CriticalSection::unlock() { (*AsPtr<std::recursive_mutex>(mStorage))->unlock(); }
bool CriticalSection::tryLock() { return (*AsPtr<std::recursive_mutex>(mStorage))->try_lock(); }

ReadWriteLock::ReadWriteLock() {
    *AsPtr<std::shared_mutex>(mStorage) = new std::shared_mutex();
    mInitialised = true;
}

ReadWriteLock::~ReadWriteLock() {
    if (mInitialised) {
        delete *AsPtr<std::shared_mutex>(mStorage);
        mInitialised = false;
    }
}

void ReadWriteLock::lockRead() { (*AsPtr<std::shared_mutex>(mStorage))->lock_shared(); }
void ReadWriteLock::lockWrite() { (*AsPtr<std::shared_mutex>(mStorage))->lock(); }
void ReadWriteLock::unlockRead() { (*AsPtr<std::shared_mutex>(mStorage))->unlock_shared(); }
void ReadWriteLock::unlockWrite() { (*AsPtr<std::shared_mutex>(mStorage))->unlock(); }

} // namespace sead
