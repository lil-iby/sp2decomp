// sys_init_tests.cpp — contract tests for Lp::Sys::SysInit.
//
// Tests nninitStartup, getCpCallbackSeadAssert, and printFromStartTime.
// nninitStartup is idempotent in the port (re-running it is safe because
// AllocateMemoryBlock always returns a fresh block and InitializeAllocator
// resets the pool).
#include "lp/sys_init.hpp"
#include "nn/init.hpp"

#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------
namespace {
int g_failures = 0, g_checks = 0;

void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else        std::printf("  ok    %s\n", what);
}
} // namespace

// ---------------------------------------------------------------------------
// Test 1 — nninitStartup completes without crashing and arms the allocator
// ---------------------------------------------------------------------------
static void test_nninitStartup() {
    std::printf("\n[1] SysInit::nninitStartup\n");

    Lp::Sys::SysInit::nninitStartup();

    // nn::init::GetDefaultAllocator() returns non-null after a successful init.
    void *allocBase = nn::init::GetDefaultAllocator();
    Check(allocBase != nullptr, "nn::init pool is armed after nninitStartup");
}

// ---------------------------------------------------------------------------
// Test 2 — nninitStartup is idempotent (second call does not crash)
// ---------------------------------------------------------------------------
static void test_nninitStartup_idempotent() {
    std::printf("\n[2] SysInit::nninitStartup — idempotent\n");

    Lp::Sys::SysInit::nninitStartup(); // first
    void *base1 = nn::init::GetDefaultAllocator();

    Lp::Sys::SysInit::nninitStartup(); // second — re-arms pool from a new block
    void *base2 = nn::init::GetDefaultAllocator();

    Check(base1 != nullptr, "pool armed after first call");
    Check(base2 != nullptr, "pool still armed after second call");
}

// ---------------------------------------------------------------------------
// Test 3 — getCpCallbackSeadAssert returns a consistent value (nullptr in port)
// ---------------------------------------------------------------------------
static void test_getCpCallback() {
    std::printf("\n[3] SysInit::getCpCallbackSeadAssert\n");

    void *cb1 = Lp::Sys::SysInit::getCpCallbackSeadAssert();
    void *cb2 = Lp::Sys::SysInit::getCpCallbackSeadAssert();

    // The port returns nullptr; the real binary returns a data symbol.
    // Both calls must return the same value (deterministic).
    Check(cb1 == cb2, "getCpCallbackSeadAssert returns consistent value");
    // No crash — that is the primary contract.
    Check(true, "getCpCallbackSeadAssert did not crash");
}

// ---------------------------------------------------------------------------
// Test 4 — printFromStartTime does not crash with various inputs
// ---------------------------------------------------------------------------
static void test_printFromStartTime() {
    std::printf("\n[4] SysInit::printFromStartTime\n");

    Lp::Sys::SysInit::printFromStartTime("boot");
    Check(true, "printFromStartTime(\"boot\") returned");

    Lp::Sys::SysInit::printFromStartTime(nullptr);
    Check(true, "printFromStartTime(nullptr) did not crash");

    Lp::Sys::SysInit::printFromStartTime("");
    Check(true, "printFromStartTime(\"\") returned");
}

// ---------------------------------------------------------------------------
// Test 5 — nn::init::Allocate works after nninitStartup
// ---------------------------------------------------------------------------
static void test_allocate_after_init() {
    std::printf("\n[5] nn::init::Allocate works after nninitStartup\n");

    Lp::Sys::SysInit::nninitStartup();

    void *p = nn::init::Allocate(64);
    Check(p != nullptr, "Allocate(64) returns non-null after nninitStartup");

    void *q = nn::init::Allocate(128, 64);
    Check(q != nullptr, "Allocate(128, align=64) returns non-null");

    // Both allocations must be distinct (no overlap).
    if (p != nullptr && q != nullptr) {
        Check(p != q, "successive allocations return distinct pointers");
    }
}

// ---------------------------------------------------------------------------
int main() {
    std::printf("Lp::Sys::SysInit tests\n");
    std::printf("(nninitStartup / getCpCallbackSeadAssert / printFromStartTime)\n");

    test_nninitStartup();
    test_nninitStartup_idempotent();
    test_getCpCallback();
    test_printFromStartTime();
    test_allocate_after_init();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
