// sead::Semaphore — an IDisposer over an nn::os counting semaphore.
//
// Binary: Semaphore::Semaphore(int, int) at 0x7100eba538 (76B, 3 call targets); PORT_WORKLIST
// lists it as the caller of nn::os::InitializeSemaphore. Interface from open-ead/sead (no
// license, written independently). lock = acquire, unlock = release.
#pragma once

#include "nn/os.hpp"
#include "sead/i_disposer.hpp"

namespace sead {

class Semaphore : public IDisposer {
public:
    Semaphore() : Semaphore(0) {}
    explicit Semaphore(int initialCount) : Semaphore(initialCount, initialCount) {}
    Semaphore(int initialCount, int maxCount);
    ~Semaphore() override;

    Semaphore(const Semaphore &) = delete;
    Semaphore &operator=(const Semaphore &) = delete;

    void lock();
    bool tryLock();
    void unlock();
    bool try_lock() { return tryLock(); }

private:
    nn::os::SemaphoreType mSemaphoreInner{}; // +0x20, the offset the binary's call sites use
};

static_assert(sizeof(Semaphore) == 0x48, "sead::Semaphore must be 0x48 bytes");

} // namespace sead
