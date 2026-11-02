// sead::Framework — its argument structs and the initialize entry point.
//
// TWO SOURCES, BOTH COMMITTED AS REFERENCE:
//   reference/seadFramework.h      -- the class and its argument structs
//   the binary's decompiled bodies -- the VALUES and the call sequence
//
// Argument constructors, ported from their decompiled bodies (literal stores, so exact):
//   sead::Framework::InitializeArg::InitializeArg()  @ 0x7100e69894
//     *(u64 *)this     = 0x3000000    // 48 MB
//     *(u64 *)(this+8) = 0
//   sead::Framework::RunArg::RunArg()                @ 0x7100e698a0
//     *(u64 *)this     = 0xffffffff00000000
//
// initialize, ported from 0x7100e698ac (200 bytes):
//   void sead::Framework::initialize(const InitializeArg& arg) {
//       if (arg.arena == nullptr) HeapMgr::initialize(arg.heap_size);
//       else                      HeapMgr::initialize(arg.arena);
//       Heap* root = <sRootHeaps[0]>;
//       Heap* th = ExpHeap::create(0, {"sead::ThreadMgr"}, root, 8, 1, 0);
//       ThreadMgr::createInstance(th); ThreadMgr::initialize(instance, th);
//       th->destroy(th);                       // vptr+0x28
//       GlobalRandom::createInstance(root);
//   }
//
// THE REFERENCE'S VALUES DO NOT ALWAYS MATCH THE BINARY'S, and the binary wins: RunArg's
// constructor stores a word mapping to stack_size=0, priority=-1, whereas the reference
// implies no particular defaults. Values here are the binary's.
#pragma once

#include <cstdint>

namespace sead {

class Arena; // forward; only its address is stored
class Heap;

class Framework {
public:
    // 0x7100e696a4, 60 bytes. The boot root constructs one of these before running.
    //
    // Decompiled body, reproduced field for field:
    //     *(Framework **)(this + 0x20) = this + 0x20;   // mResetEvent: self-linked node
    //     *(Framework **)(this + 0x28) = this + 0x20;
    //     this[8] = 0;                                  // +0x08 mReserveReset = false
    //     *(u32 *)(this + 0x30) = 0;                    // mResetEvent count/field
    //     *(u64 *)(this + 0x40) = 0;                    // mTaskMgr
    //     *(u64 *)(this + 0x48) = 0;                    // mMethodTreeMgr
    //     *(void **)this = <Framework vtable>;          // +0x00
    //     *(u64 *)(this + 0x38) = 0;                    // mResetParameter
    //     *(u64 *)(this + 0x10) = 0;                    // vtable slot / base field
    //     *(void **)(this + 0x18) = <ResetEvent vtable>;// +0x18
    Framework();

    // The offsets the constructor writes, asserted in the implementation so the model cannot
    // drift from them -- the same guard that has caught four layout errors in this port.
    static constexpr unsigned kResetEventOffset = 0x20;
    static constexpr unsigned kTaskMgrOffset = 0x40;

    struct InitializeArg {
        InitializeArg();

        std::uint64_t heap_size; // +0x00, observable default 0x3000000 (48 MB)
        Arena *arena;            // +0x08, observable default null
    };

    struct RunArg {
        RunArg();

        // The constructor writes one 64-bit word, 0xffffffff00000000. The port stores those
        // bytes verbatim (see framework_args.cpp), which is byte-exact and cannot be got
        // wrong by reasoning about halves or endianness -- four attempts proved that.
        std::uint32_t prepare_stack_size; // +0x00
        std::int32_t prepare_priority;    // +0x04
    };

    // 0x7100e698ac, 200 bytes. First consumer of InitializeArg; calls the ported
    // HeapMgr::initialize overloads.
    static void initialize(const InitializeArg &arg);

    // The heap the ThreadMgr was given, retained so a later stage can inspect or restore it.
    // The original reaches it through PTR_sInstance_7102f9d280.
    static Heap *threadMgrHeap();

    // 0x7100e69908 (estimated). The main render loop. Entered by SysInit::Init after
    // completing the 14-step framework setup. In the binary this blocks until the game exits;
    // in the port it exits when requestStop() is called or the frame limit is reached.
    //
    // Minimal implementation:  BeginFrame → EndFrame per frame, Win32 message pump on
    // Windows. Task manager dispatch (calc_, draw_) will be wired in once WorkerThreadMgr
    // is ported.
    void run(const RunArg &arg);

    // Request a clean exit from run(). Thread-safe.
    static void requestStop();

    // Set a hard frame limit. run() exits after this many frames.
    // -1 (default) means "run until requestStop() or window close".
    // Not in the original binary; present so automated tests can run a bounded loop.
    static void setFrameLimit(int limit);

    // --- the modelled layout, at the offsets the constructor writes ---------------------
    // Not part of the original's public surface; present so the constructor's stores land in
    // the right places and the offsets can be asserted.
    void *mVtable = nullptr;        // +0x00
    unsigned char mPad08[0x08] = {}; // +0x08 (this[8] = 0)
    void *mField10 = nullptr;       // +0x10
    void *mResetEventVtable = nullptr; // +0x18 (PTR_vtable_7102fc9f88 + 0x10)
    void *mResetEventHead = nullptr;   // +0x20 (self-linked)
    void *mResetEventTail = nullptr;   // +0x28 (self-linked to +0x20)
    unsigned int mResetEventCount = 0; // +0x30
    void *mResetParameter = nullptr;   // +0x38
    void *mTaskMgr = nullptr;          // +0x40
    void *mMethodTreeMgr = nullptr;    // +0x48
    void *mMethodTreeMgrHeap = nullptr; // +0x50
};

} // namespace sead
