// run_loop_tests.cpp — verifies that Framework::run drives BeginFrame→EndFrame
// for a requested number of frames and then exits cleanly.
//
// These tests ONLY exercise the stub backend path (NVN_BACKEND_VULKAN is OFF
// in CI), so they never open a real window.  With the Vulkan backend the same
// test also exercises real swapchain acquire/present.
#include "sead/framework_args.hpp"
#include "sead/game_framework_nx.hpp"
#include "nvn/nvn_bridge.hpp"
#include "nn/vi.hpp"

#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------
namespace {
int g_failures = 0, g_checks = 0;

void Check(bool cond, const char* what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    } else {
        std::printf("  ok    %s\n", what);
    }
}
} // namespace

// ---------------------------------------------------------------------------
// Helpers: full init sequence (mirrors boot_sequence.cpp up to step 12).
// ---------------------------------------------------------------------------
static void bootAndInit() {
    sead::Framework::InitializeArg initArg;
    sead::Framework::initialize(initArg);

    sead::GameFrameworkNx::InitializeArg gfxArg;
    sead::GameFrameworkNx::initialize(gfxArg);

    sead::GameFrameworkNx::initializeGraphicsSystem();
}

// ---------------------------------------------------------------------------
// Test 1 — bounded run of N frames
// ---------------------------------------------------------------------------
static void test_bounded_run() {
    std::printf("\n[1] Framework::run — bounded loop (3 frames)\n");

    bootAndInit();
    nvn::bridge::ResetStats();

    sead::Framework::setFrameLimit(3);

    sead::Framework fw;
    sead::Framework::RunArg runArg;
    fw.run(runArg);

    const auto stats = nvn::bridge::GetStats();
    Check(stats.framesRendered == 3, "exactly 3 frames rendered (BeginFrame→EndFrame)");
    Check(stats.drawCallsLastFrame == 0,
          "no draw calls yet — stub backend / task-manager not yet ported");

    // Restore unlimited for subsequent tests.
    sead::Framework::setFrameLimit(-1);
    nn::vi::ResetForTesting();
}

// ---------------------------------------------------------------------------
// Test 2 — requestStop exits the loop immediately
// ---------------------------------------------------------------------------
static void test_request_stop() {
    std::printf("\n[2] Framework::requestStop — exits loop early\n");

    bootAndInit();
    nvn::bridge::ResetStats();

    // Unlimited frame limit; we call requestStop() from the test thread
    // BEFORE calling run() — the loop should complete 0 frames.
    sead::Framework::setFrameLimit(-1);
    sead::Framework::requestStop(); // signal before entering run()

    sead::Framework fw;
    sead::Framework::RunArg runArg;
    fw.run(runArg);

    const auto stats = nvn::bridge::GetStats();
    // After requestStop() the loop must exit immediately (0 frames).
    Check(stats.framesRendered == 0,
          "run() exits immediately when requestStop() was already called");

    sead::Framework::setFrameLimit(-1); // restore
    nn::vi::ResetForTesting();
}

// ---------------------------------------------------------------------------
// Test 3 — a second run starts fresh after a previous bounded run
// ---------------------------------------------------------------------------
static void test_run_is_reentrant() {
    std::printf("\n[3] Framework::run — re-entrant: second call starts fresh\n");

    bootAndInit();
    nvn::bridge::ResetStats();

    sead::Framework fw;
    sead::Framework::RunArg runArg;

    sead::Framework::setFrameLimit(2);
    fw.run(runArg);  // 2 frames

    nvn::bridge::ResetStats();

    sead::Framework::setFrameLimit(4);
    fw.run(runArg);  // 4 more frames

    const auto stats = nvn::bridge::GetStats();
    Check(stats.framesRendered == 4,
          "second run renders exactly 4 frames (gStopRequested reset at entry)");

    sead::Framework::setFrameLimit(-1);
    nn::vi::ResetForTesting();
}

// ---------------------------------------------------------------------------
int main() {
    std::printf("sead::Framework::run loop tests\n");
    std::printf("backend: %s\n", nvn::bridge::GetActiveBackendName());

    test_bounded_run();
    test_request_stop();
    test_run_is_reentrant();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
