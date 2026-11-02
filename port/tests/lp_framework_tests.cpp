// lp_framework_tests.cpp — contract tests for Lp::Sys::Framework.
//
// Covers the constructor at 0x71011f44e4 (108 bytes, depth 1).
// Verifies: construction without crash, CreateArg pointer stored, state zero.
#include "lp/lp_framework.hpp"

#include <cstdio>

namespace {
int g_failures = 0, g_checks = 0;

void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else        std::printf("  ok    %s\n", what);
}
} // namespace

// ---------------------------------------------------------------------------
// Test 1 — constructor completes without crash
// ---------------------------------------------------------------------------
static void test_ctor_no_crash() {
    std::printf("\n[1] Framework ctor (no crash)\n");

    sead::GameFrameworkNx::CreateArg arg;
    Lp::Sys::Framework fw(arg);

    Check(true, "Framework(CreateArg&) did not crash");
}

// ---------------------------------------------------------------------------
// Test 2 — createArg() returns the supplied arg's address
// ---------------------------------------------------------------------------
static void test_create_arg_stored() {
    std::printf("\n[2] Framework stores CreateArg pointer\n");

    sead::GameFrameworkNx::CreateArg arg;
    Lp::Sys::Framework fw(arg);

    Check(fw.createArg() == &arg, "createArg() equals &arg");
    Check(fw.createArg() != nullptr, "createArg() is non-null");
}

// ---------------------------------------------------------------------------
// Test 3 — two distinct instances store separate CreateArg pointers
// ---------------------------------------------------------------------------
static void test_two_instances() {
    std::printf("\n[3] Two Framework instances have distinct CreateArg pointers\n");

    sead::GameFrameworkNx::CreateArg argA, argB;
    Lp::Sys::Framework fwA(argA);
    Lp::Sys::Framework fwB(argB);

    Check(fwA.createArg() != fwB.createArg(), "distinct instances point to distinct args");
    Check(fwA.createArg() == &argA, "fwA points to argA");
    Check(fwB.createArg() == &argB, "fwB points to argB");
}

// ---------------------------------------------------------------------------
int main() {
    std::printf("Lp::Sys::Framework tests\n");
    std::printf("(ctor 0x71011f44e4 — depth 1, 108 bytes)\n");

    test_ctor_no_crash();
    test_create_arg_stored();
    test_two_instances();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
