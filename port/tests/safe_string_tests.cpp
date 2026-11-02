// safe_string_tests.cpp — contract tests for sead string classes.
//
// Covers:
//   SafeStringBase<char>          — immutable C-string view
//   BufferedSafeStringBase<char>  — mutable fixed-capacity string
//   FormatFixedSafeString<N>      — stack-allocated formatted string
#include "sead/safe_string.hpp"

#include <cstdio>
#include <cstring>

namespace {
int g_failures = 0, g_checks = 0;

void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else        std::printf("  ok    %s\n", what);
}
} // namespace

// ---------------------------------------------------------------------------
// Test 1 — SafeStringBase default and string constructors
// ---------------------------------------------------------------------------
static void test_safe_string_base() {
    std::printf("\n[1] SafeStringBase<char>\n");

    sead::SafeStringBase<char> empty;
    Check(empty.isEmpty(), "default-constructed string is empty");
    Check(empty.cstr() != nullptr, "default cstr() is not null (points to sEmpty)");
    Check(empty.cstr()[0] == '\0', "default cstr()[0] is NUL");

    sead::SafeStringBase<char> hello("hello");
    Check(!hello.isEmpty(), "non-empty string is not empty");
    Check(std::strcmp(hello.cstr(), "hello") == 0, "cstr() returns the original string");

    sead::SafeStringBase<char> fromNull(nullptr);
    Check(fromNull.cstr() != nullptr, "null-constructed cstr() falls back to sEmpty");
    Check(fromNull.cstr()[0] == '\0', "null-constructed string is effectively empty");
}

// ---------------------------------------------------------------------------
// Test 2 — BufferedSafeStringBase: buffer pointer, capacity, assureTermination
// ---------------------------------------------------------------------------
static void test_buffered_safe_string_base() {
    std::printf("\n[2] BufferedSafeStringBase<char>\n");

    char buf[32];
    sead::BufferedSafeStringBase<char> s(buf, 32);

    Check(s.getBuffer() == buf, "getBuffer() returns the supplied buffer");
    Check(s.getBufferSize() == 32, "getBufferSize() returns supplied capacity");
    Check(buf[0] == '\0', "ctor NUL-terminates the buffer");

    // assureTerminationImpl_ should write NUL at the end.
    buf[31] = 'X';
    s.assureTerminationImpl_();
    Check(buf[31] == '\0', "assureTerminationImpl_ writes NUL at [capacity-1]");
}

// ---------------------------------------------------------------------------
// Test 3 — BufferedSafeStringBase::format
// ---------------------------------------------------------------------------
static void test_buffered_format() {
    std::printf("\n[3] BufferedSafeStringBase<char>::format\n");

    char buf[64];
    sead::BufferedSafeStringBase<char> s(buf, 64);

    const int n = s.format("boot step %d: %s", 7, "complete");
    Check(n > 0, "format() returns positive byte count");
    Check(std::strcmp(s.cstr(), "boot step 7: complete") == 0,
          "formatted string matches expected text");
    Check(buf[63] == '\0', "buffer is always NUL-terminated at [capacity-1]");
}

// ---------------------------------------------------------------------------
// Test 4 — FormatFixedSafeString<N>: basic usage
// ---------------------------------------------------------------------------
static void test_format_fixed_safe_string_basic() {
    std::printf("\n[4] FormatFixedSafeString<N> basic\n");

    sead::FormatFixedSafeString<32> s("value=%d", 42);
    Check(std::strcmp(s.cstr(), "value=42") == 0, "formatted content is correct");
    Check(s.getBufferSize() == 32, "buffer size equals template argument N");
}

// ---------------------------------------------------------------------------
// Test 5 — FormatFixedSafeString<N>: overflow is safely truncated
// ---------------------------------------------------------------------------
static void test_format_fixed_safe_string_overflow() {
    std::printf("\n[5] FormatFixedSafeString<N> overflow\n");

    // A 4-byte buffer holds at most 3 chars + NUL.
    sead::FormatFixedSafeString<4> s("abcdefgh");
    Check(s.cstr()[3] == '\0', "NUL-terminated even when fmt overflows capacity");
    Check(std::strlen(s.cstr()) <= 3, "string length does not exceed capacity-1");
}

// ---------------------------------------------------------------------------
// Test 6 — FormatFixedSafeString<N>: nullptr fmt is safe
// ---------------------------------------------------------------------------
static void test_format_fixed_safe_string_null() {
    std::printf("\n[6] FormatFixedSafeString<N> nullptr fmt\n");

    sead::FormatFixedSafeString<16> s(nullptr);
    // Should not crash; the buffer is at least NUL-terminated.
    Check(s.cstr() != nullptr, "cstr() is not null after nullptr fmt");
    Check(s.getBufferSize() == 16, "capacity is still 16");
    // content is whatever the default-constructed buffer was (NUL from BufferedSafeStringBase ctor)
    Check(true, "did not crash");
}

// ---------------------------------------------------------------------------
// Test 7 — Polymorphism: assureTerminationImpl_ is called through vtable
// ---------------------------------------------------------------------------
static void test_virtual_dispatch() {
    std::printf("\n[7] Virtual assureTerminationImpl_ dispatch\n");

    char buf[8];
    sead::SafeStringBase<char> *base =
        new sead::BufferedSafeStringBase<char>(buf, 8);

    // Corrupt the last byte, then call through the base pointer.
    buf[7] = 'Z';
    base->assureTerminationImpl_();
    Check(buf[7] == '\0', "virtual dispatch reaches BufferedSafeStringBase override");

    delete base;
}

// ---------------------------------------------------------------------------
int main() {
    std::printf("sead safe-string tests\n");
    std::printf("(SafeStringBase / BufferedSafeStringBase / FormatFixedSafeString)\n");

    test_safe_string_base();
    test_buffered_safe_string_base();
    test_buffered_format();
    test_format_fixed_safe_string_basic();
    test_format_fixed_safe_string_overflow();
    test_format_fixed_safe_string_null();
    test_virtual_dispatch();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
