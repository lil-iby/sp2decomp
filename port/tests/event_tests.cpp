// event_tests.cpp — contract tests for sead::Event and the nn::os light-event layer.
#include "sead/event.hpp"
#include "sead/heap.hpp"
#include "sead/heap_mgr.hpp"
#include "nn/os.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

namespace {
int g_failures = 0, g_checks = 0;

void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else        std::printf("  ok    %s\n", what);
}

const sead::TickSpan kShort = sead::TickSpan::fromMilliSeconds(5);
} // namespace

static void test_convert_to_time_span() {
    std::printf("\n[1] nn::os::ConvertToTimeSpan — ticks to nanoseconds\n");
    Check(nn::os::ConvertToTimeSpan(0) == 0, "0 ticks -> 0 ns");
    Check(nn::os::ConvertToTimeSpan(19'200'000) == 1'000'000'000, "19.2M ticks -> 1 s");
    Check(nn::os::ConvertToTimeSpan(19'200) == 1'000'000, "19200 ticks -> 1 ms");
    const std::int64_t day = 19'200'000LL * 86'400;
    Check(nn::os::ConvertToTimeSpan(day) == 86'400LL * 1'000'000'000, "one day does not overflow");
}

static void test_manual_reset() {
    std::printf("\n[2] manual-reset event stays signalled until reset\n");
    sead::Event ev(true);
    Check(!ev.wait(kShort), "unsignalled wait times out");
    ev.setSignal();
    Check(ev.wait(kShort), "first wait succeeds");
    Check(ev.wait(kShort), "second wait still succeeds (manual reset)");
    ev.resetSignal();
    Check(!ev.wait(kShort), "wait times out after resetSignal");
}

static void test_auto_reset() {
    std::printf("\n[3] auto-reset event is consumed by one wait\n");
    sead::Event ev(false);
    ev.setSignal();
    Check(ev.wait(kShort), "first wait succeeds");
    Check(!ev.wait(kShort), "second wait times out (auto reset)");
}

static void test_cross_thread() {
    std::printf("\n[4] another thread wakes a blocking wait\n");
    sead::Event ev(false);
    std::thread signaller([&ev] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ev.setSignal();
    });
    ev.wait(); // must return
    signaller.join();
    Check(true, "blocking wait() returned after setSignal from another thread");
}

static void test_uninitialised() {
    std::printf("\n[5] uninitialised event\n");
    {
        sead::Event ev;
        Check(!ev.wait(kShort), "timed wait on uninitialised event returns false");
        ev.setSignal(); // no-op, must not crash
    }
    Check(true, "destroying an uninitialised event did not crash");
}

static void test_is_disposer() {
    std::printf("\n[6] Event is an IDisposer\n");
    sead::HeapMgr::initialize(1024 * 1024);
    sead::Heap *root = sead::HeapMgr::getRootHeap(0);
    const std::size_t before = root->disposerCount();
    {
        sead::Event ev(root, true);
        Check(ev.getDisposerHeap() == root, "registered with the supplied heap");
        Check(root->disposerCount() == before + 1, "heap disposer count +1");
    }
    Check(root->disposerCount() == before, "unregistered on destruction");
    sead::HeapMgr::destroy();
}

int main() {
    std::printf("sead::Event tests\n");
    test_convert_to_time_span();
    test_manual_reset();
    test_auto_reset();
    test_cross_thread();
    test_uninitialised();
    test_is_disposer();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
