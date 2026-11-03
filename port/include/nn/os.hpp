// nn::os — platform layer contracts, reconstructed from call-site evidence.
//
// The real implementations are NOT in the game dump: `main` imports these and the
// system provides them at runtime (see docs/PLATFORM_LAYER_LOCATION.md). Every
// signature below is therefore derived from how `main` calls the function, taken from
// the whole-binary call-site scan (48 imports, 1712 call sites, 1191 callers) recorded
// in docs/PLATFORM_IMPLEMENTATION_SPEC.md.
//
// Argument shapes quoted in the comments are literal call expressions from Ghidra
// pseudocode, so arity is observed rather than guessed.
#pragma once

#include <cstdint>
#include <cstddef>

namespace nn::os {

// ---- fixed ABI types -----------------------------------------------------
// These are the SDK's own opaque handles. The game allocates storage for them inside
// its own objects and passes the address, e.g. `nn::os::LockMutex((MutexType *)(this +
// 0x348))`.
//
// SIZES ARE EVIDENCE-BASED. See docs/TYPE_SIZES.md for the method and the caveats; the
// short version is that the game always allocates these as SINGLE FIELDS, never as
// array elements, so the host backing store only has to fit the host primitive — the
// SDK never computes sizeof() for them. Where a size is measurable from the binary it is
// recorded below; where it is not, that is stated rather than guessed.
struct MutexType {
    // 0x20. Two `.bss` globals initialised 0x20 apart suggest sizeof <= 0x20, and
    // sead::CriticalSection = IDisposer (0x20) + MutexType must total 0x40 for
    // Lp::Sys::HeapGroup's offsets (+0x10 CriticalSection, +0x50 ReadWriteLock) to hold.
    // The host mutex lives behind a pointer (see os.cpp), so 0x20 is ample.
    alignas(16) unsigned char storage[0x20];
};

struct SemaphoreType {
    // UNBOUNDED by the evidence: four call sites consistently use `this + 0x20`, which
    // confirms a stable field offset but reveals no following sibling field.
    // The host needs 96 bytes for {mutex, condition_variable, two ints}, so this is sized
    // to the HOST requirement, not to a measured SDK size. Safe here for the same reason
    // as MutexType: every observed site allocates a single field, never an array element
    // whose stride the SDK would have to know.
    alignas(16) unsigned char storage[0x60];
};

struct ThreadType {
    // UNOBSERVABLE. Created by the SDK via CreateThread and held BY POINTER at three of
    // four observed sites, so the game never exercises its size inline. The game also
    // never reads fields inside it in the observed code, so any adequate host reserve
    // works.
    alignas(16) unsigned char storage[0x80];
};

struct EventType {
    // MEASURED CORRECTION: 0x40 was too small. The host event state (mutex + condition
    // variable + flags) needs more than 0x40 bytes on x86-64, and placement-new overran it --
    // caught by a static_assert. Horizon's own EventType is larger than 0x40 as well, so the
    // earlier figure was simply wrong. Sized to the requirement, documented rather than
    // silently bumped.
    alignas(16) unsigned char storage[0x80];
};

// nn::vi::GetDisplayVsyncEvent takes an nn::os::SystemEventType by out-pointer. Modelled as
// the SDK's system-event spelling of EventType so the display layer and the render loop agree
// on one type.
using SystemEventType = EventType;

// The clear mode GetDisplayVsyncEvent / CreateSystemEvent use. AutoClear is what a per-frame
// vsync wait needs: the event resets itself once signalled.
enum class EventClearMode : int {
    AutoClear = 0,
    ManualClear = 1,
};

// 0x7100e6738c's path reaches nn::vi::GetDisplayVsyncEvent, which must produce a usable
// event. Signal/reset/wait are the operations the render loop performs on it.
int CreateSystemEvent(SystemEventType *outEvent, EventClearMode clearMode, bool initiallySignaled);
void SignalSystemEvent(SystemEventType *event);
void ClearSystemEvent(SystemEventType *event);
void WaitSystemEvent(SystemEventType *event);

// Returned by QueryMemoryInfo; the game passes a pointer it owns.
struct MemoryInfo {
    std::uint64_t totalSize;      // observed: QueryMemoryInfo((MemoryInfo *)&local_58)
    std::uint64_t usedSize;
};

// Message queue storage. MEASURED: four call sites pass the queue and its backing
// buffer as sibling fields of the same object, always exactly 0x48 apart, so the
// queue's region is 0x48 — the only size in this header established by measurement.
// The count is a runtime capacity (0x20 and 0x21 both appear), not part of the type.
struct MessageQueueType {
    alignas(16) unsigned char storage[0x48];
};

// Thread-local-destructor callback passed to AllocateTlsSlot.
using TlsDestructor = void (*)(std::uint64_t);

// Stack region reported by GetCurrentStackInfo.
struct StackInfo {
    std::uintptr_t base;
    std::uintptr_t size;
};

// ---- time ----------------------------------------------------------------
// `uVar5 = nn::os::GetSystemTick();`  — 677 call sites across 615 callers.
// Zero arguments, 64-bit result. The Switch tick frequency is 19.2 MHz.
constexpr std::uint64_t kTickFrequencyHz = 19'200'000;
std::uint64_t GetSystemTick();

// `nn::os::SleepThread(5000000);` — 23 call sites, nanoseconds.
// A call with no argument is also observed (`nn::os::SleepThread();`), which is the
// SDK's "sleep indefinitely" form; default it to zero so the arity matches both.
void SleepThread(std::int64_t nanoseconds = 0);

// `lVar9 = nn::os::ConvertToTimeSpan(lVar7 - lVar9);` — 21 call sites. The argument is a
// difference of two GetSystemTick values, so this converts TICKS to a nanosecond
// TimeSpan (sead::Thread::sleep feeds the result to SleepThread, which takes ns).
std::int64_t ConvertToTimeSpan(std::int64_t ticks);

// ---- light events ---------------------------------------------------------
// The SDK's kernel-free event, which sead::Event wraps. Its size is not observed in the
// boot closure; storage holds a pointer to host state, like EventType.
struct LightEventType {
    alignas(8) unsigned char storage[0x10] = {};
};
void InitializeLightEvent(LightEventType *event, bool initiallySignaled, EventClearMode clearMode);
void FinalizeLightEvent(LightEventType *event);
void SignalLightEvent(LightEventType *event);
void ClearLightEvent(LightEventType *event);
void WaitLightEvent(LightEventType *event);
// Returns true if the event was signalled before the timeout (nanoseconds) expired.
bool TimedWaitLightEvent(LightEventType *event, std::int64_t timeoutNs);

// ---- memory --------------------------------------------------------------
// `nn::os::AllocateMemoryBlock(&local_18, 0x2000000);` — the first argument is an OUT
// pointer for the allocation, the second a byte size (observably rounded up to 2 MB).
// Returns a result code that callers test.
int AllocateMemoryBlock(std::uint64_t *outAddress, std::uint64_t size);
// Releases a block from AllocateMemoryBlock. The host block is an aligned allocation, so
// std::free on it is wrong (heap corruption on Windows); always come through here.
void FreeMemoryBlock(std::uint64_t address, std::uint64_t size);
// `nn::os::QueryMemoryInfo((MemoryInfo *)&local_58);`
void QueryMemoryInfo(MemoryInfo *out);
// `nn::os::SetMemoryHeapSize(uVar1);`
void SetMemoryHeapSize(std::uint64_t size);

// ---- TLS -----------------------------------------------------------------
// `lVar8 = nn::os::GetTlsValue(*(undefined4 *)puVar6);`   — 264 call sites.
// `nn::os::SetTlsValue(*(undefined4 *)(this + 0x88), this_00);` — 8 call sites.
void *GetTlsValue(std::uint32_t slot);
void SetTlsValue(std::uint32_t slot, void *value);
// `nn::os::AllocateTlsSlot(this + 0x88, (_func_void_ulong *)0x0);` — the first argument
// is an OUT pointer receiving the slot number, the second an optional destructor.
int AllocateTlsSlot(std::uint32_t *outSlot, TlsDestructor destructor);

// ---- synchronisation -----------------------------------------------------
// `nn::os::InitializeMutex(pMVar9, false, 0);` — 56 call sites.
// Args: storage, recursive flag, priority-inheritance flag.
void InitializeMutex(MutexType *mutex, bool recursive, int priorityInheritance);
// `nn::os::LockMutex((MutexType *)(this + 0x348));`   — 171 call sites.
void LockMutex(MutexType *mutex);
// `nn::os::UnlockMutex((MutexType *)(puVar3 + 0x768));` — 170 call sites.
void UnlockMutex(MutexType *mutex);
bool TryLockMutex(MutexType *mutex);
void FinalizeMutex(MutexType *mutex);

void InitializeSemaphore(SemaphoreType *semaphore, int initial, int max);
void AcquireSemaphore(SemaphoreType *semaphore);
void ReleaseSemaphore(SemaphoreType *semaphore);

// `InitializeMessageQueue((MessageQueueType *)(this + 0x1f8), (ulong *)(this + 0x240), 0x20);`
// A bounded FIFO of 64-bit values held in the caller's buffer. Send blocks while full,
// Receive/Peek while empty; Jam inserts at the front. The Try* forms never block.
void InitializeMessageQueue(MessageQueueType *queue, std::uint64_t *buffer, std::size_t count);
void FinalizeMessageQueue(MessageQueueType *queue);
void SendMessageQueue(MessageQueueType *queue, std::uint64_t message);
bool TrySendMessageQueue(MessageQueueType *queue, std::uint64_t message);
void JamMessageQueue(MessageQueueType *queue, std::uint64_t message);
bool TryJamMessageQueue(MessageQueueType *queue, std::uint64_t message);
void ReceiveMessageQueue(std::uint64_t *outMessage, MessageQueueType *queue);
bool TryReceiveMessageQueue(std::uint64_t *outMessage, MessageQueueType *queue);
void PeekMessageQueue(std::uint64_t *outMessage, const MessageQueueType *queue);
bool TryPeekMessageQueue(std::uint64_t *outMessage, const MessageQueueType *queue);

// ---- CPU / threads -------------------------------------------------------
// `uVar7 = nn::os::GetCurrentCoreNumber();` — 121 call sites.
std::uint32_t GetCurrentCoreNumber();
std::uint64_t GetThreadAvailableCoreMask();
// `nn::os::SetThreadCoreMask(*(ThreadType **)(this + 0xe0), -1, (ulong)uVar1);`
// core == -1 means "any core in mask".
void SetThreadCoreMask(ThreadType *thread, int core, std::uint64_t mask);
void ChangeThreadPriority(ThreadType *thread, int priority);
// The calling thread's ThreadType: the one given to CreateThread for threads created here,
// otherwise one adopted for the host thread on first use (e.g. the main thread). Stable for
// the thread's lifetime. Signatures follow the SDK as recorded in tools/wsl/hle_os.cpp.
ThreadType *GetCurrentThread();
// A non-zero id unique to the thread; 0 for null.
std::uint64_t GetThreadId(const ThreadType *thread);
int GetThreadPriority(const ThreadType *thread);
void SetThreadName(ThreadType *thread, const char *name);
// `nn::os::GetCurrentStackInfo((ulong *)&local_38, (ulong *)local_48);` — two OUT params.
void GetCurrentStackInfo(std::uintptr_t *outBase, std::uintptr_t *outSize);

// Called by sead::Thread. The thread is created suspended and runs `function(argument)`
// once StartThread is called. The caller's stack buffer is accepted but not used: the
// host thread owns its own stack.
using ThreadFunction = void (*)(void *);
int CreateThread(ThreadType *thread, ThreadFunction function, void *argument, void *stack,
                 std::size_t stackSize, int priority);
void StartThread(ThreadType *thread);
void WaitThread(ThreadType *thread);
void DestroyThread(ThreadType *thread);
void YieldThread();

} // namespace nn::os
