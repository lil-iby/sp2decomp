// sead::Framework::initialize — ported from 0x7100e698ac (200 bytes).
// sead::Framework::run          — ported from ~0x7100e69908 (estimated).
#include "sead/framework_args.hpp"

#include "sead/heap.hpp"
#include "sead/heap_mgr.hpp"
#include "sead/thread_mgr.hpp"
#include "nvn/nvn_bridge.hpp"

#include <atomic>
#include <cstdio>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace sead {

namespace {
Heap *gThreadMgrHeap = nullptr;

// Run-loop state. Written only through Framework::requestStop / setFrameLimit;
// read from a single thread inside run(), so relaxed ordering is sufficient.
std::atomic<bool> gStopRequested{false};
std::atomic<int>  gFrameLimit{-1}; // -1 = run until requestStop() or window close
} // namespace

Heap *Framework::threadMgrHeap() { return gThreadMgrHeap; }

void Framework::initialize(const InitializeArg &arg) {
    // Clear any heap recorded by a previous call FIRST, so a failed or refused
    // initialisation cannot leave a dangling ThreadMgr heap. The refusal-path test caught
    // exactly that: without this, threadMgrHeap() reported the PREVIOUS run's heap.
    gThreadMgrHeap = nullptr;

    // --- the arena choice the decompiled body makes -------------------------------------
    //   if ((undefined8 *)param_1[1] == 0) HeapMgr::initialize(*param_1);
    //   else                               HeapMgr::initialize(param_1[1]);
    // arg.arena is at +0x08 and arg.heap_size at +0x00, matching the decompile's param_1[1]
    // and *param_1.
    if (arg.arena == nullptr) {
        HeapMgr::initialize(static_cast<std::size_t>(arg.heap_size));
    } else {
        HeapMgr::initialize(arg.arena);
    }

    // --- the root heap, which the boot path reads as sRootHeaps[0] ----------------------
    Heap *root = HeapMgr::getRootHeap(0);
    if (root == nullptr) return; // initialize() refused (null arena / allocation failure)

    // --- ThreadMgr heap ----------------------------------------------------------------
    //   pHVar1 = (Heap *)ExpHeap::create(0, (long)&local_30, pHVar2, 8, 1, 0);
    // Note the literal size 0, reproduced rather than corrected.
    ExpHeap *th = ExpHeap::create(0, "sead::ThreadMgr", root, 8, HeapDirection::eForward, true);
    if (th == nullptr) return;
    gThreadMgrHeap = th;

    //   ThreadMgr::createInstance(pHVar1);
    //   ThreadMgr::initialize(*(ThreadMgr **)PTR_sInstance_7102f9d280, pHVar1);
    ThreadMgr *mgr = ThreadMgr::createInstance(th);
    ThreadMgr::initialize(mgr, th);

    //   (**(code **)(*(long *)pHVar1 + 0x28))(pHVar1);   -- vptr+0x28, Heap::destroy
    th->destroy();

    //   GlobalRandom::createInstance(pHVar2);
    GlobalRandom::createInstance(root);
}

void Framework::requestStop() {
    gStopRequested.store(true, std::memory_order_relaxed);
}

void Framework::setFrameLimit(int limit) {
    gFrameLimit.store(limit, std::memory_order_relaxed);
}

void Framework::run(const RunArg &arg) {
    (void)arg; // prepare_stack_size / prepare_priority drive the task-manager thread setup
               // (WorkerThreadMgr); not yet ported — will wire in once WorkerThreadMgr lands.

    gStopRequested.store(false, std::memory_order_relaxed);
    int frameCount = 0;
    const int limit = gFrameLimit.load(std::memory_order_relaxed);

    while (!gStopRequested.load(std::memory_order_relaxed)) {
        if (limit >= 0 && frameCount >= limit) break;

#if defined(_WIN32)
        // Drain the Win32 message queue so the window stays responsive.
        // WM_QUIT means the user closed the window; honour it immediately.
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) {
                gStopRequested.store(true, std::memory_order_relaxed);
                break;
            }
        }
        if (gStopRequested) break;
#endif

        nvn::bridge::BeginFrame();
        // Future: calc_() and draw_() dispatched through WorkerThreadMgr / TaskMgr.
        nvn::bridge::EndFrame();
        ++frameCount;
    }

    std::printf("sead::Framework::run: completed %d frame(s)\n", frameCount);
}

} // namespace sead
