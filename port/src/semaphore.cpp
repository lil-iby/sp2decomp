// sead::Semaphore — see port/include/sead/semaphore.hpp.
#include "sead/semaphore.hpp"

namespace sead {

Semaphore::Semaphore(int initialCount, int maxCount) : IDisposer() {
    nn::os::InitializeSemaphore(&mSemaphoreInner, initialCount, maxCount);
}

Semaphore::~Semaphore() { nn::os::FinalizeSemaphore(&mSemaphoreInner); }

void Semaphore::lock() { nn::os::AcquireSemaphore(&mSemaphoreInner); }

bool Semaphore::tryLock() { return nn::os::TryAcquireSemaphore(&mSemaphoreInner); }

void Semaphore::unlock() { nn::os::ReleaseSemaphore(&mSemaphoreInner); }

} // namespace sead
