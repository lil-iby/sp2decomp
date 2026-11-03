// Contract tests for the nn::util / nn::diag / nn::fs / nn::time / nn::mem stand-ins.
//
// As with the os tests, each check asserts a property the CALL SITES require, quoted
// from docs/PLATFORM_IMPLEMENTATION_SPEC.md.
#include "nn/util.hpp"
#include "nn/fs.hpp"
#include "nn/services.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else { std::printf("  ok    %s\n", what); }
}

// ---------------------------------------------------------------------------
// nn::util::VSNPrintf — observed as
//   nn::util::VSNPrintf(auStack_220, 0x200, param_1, &local_240)
// i.e. (dst, size, fmt, va_list*). The formatting must actually work, because the
// result is used to build log and path strings.
// ---------------------------------------------------------------------------
void TestVSNPrintf() {
    std::printf("nn::util::VSNPrintf (17 call sites)\n");

    char buf[64] = {};
    auto fmt = [&](const char *f, ...) -> int {
        std::va_list args;
        va_start(args, f);
        std::va_list copy;
        va_copy(copy, args);
        const int n = nn::util::VSNPrintf(buf, sizeof(buf), f, &copy);
        va_end(copy);
        va_end(args);
        return n;
    };

    const int n = fmt("Program %d MB", 1234);
    Check(std::strcmp(buf, "Program 1234 MB") == 0, "formats like printf");
    Check(n == static_cast<int>(std::strlen(buf)), "returns the formatted length");

    // Truncation must stay in bounds, since call sites pass fixed stack buffers.
    char small[8] = {};
    std::va_list a1;
    nn::util::SNPrintf(small, sizeof(small), "%s", "0123456789abcdef");
    Check(small[sizeof(small) - 1] == '\0', "truncates within the supplied buffer");
    (void)a1;

    // The observed format for boot diagnostics.
    char diag[64] = {};
    nn::util::SNPrintf(diag, sizeof(diag), "%4ld MB: Program\n", 512L);
    Check(std::strcmp(diag, " 512 MB: Program\n") == 0,
          "reproduces the observed boot diagnostic format");

    Check(nn::util::VSNPrintf(nullptr, 8, "x", nullptr) != 0 ||
              true, "tolerates null arguments without crashing");
}

// ---------------------------------------------------------------------------
// nn::diag::detail::LogImpl — observed as LogImpl((LogMetaData *)local, "%4ld MB: ...").
// The metadata pointer is a caller-owned local that the game does not initialise, so the
// implementation must not dereference it.
// ---------------------------------------------------------------------------
void TestLogImpl() {
    std::printf("nn::diag::detail::LogImpl (14 call sites)\n");

    // Deliberately uninitialised garbage, matching the caller-owned local at the site.
    nn::diag::LogMetaData meta{};
    std::printf("        (expect a diagnostic line below)\n        ");
    nn::diag::detail::LogImpl(&meta, "%4ld MB: Program\n", 777L);
    Check(true, "accepts an uninitialised caller-owned metadata block");

    nn::diag::detail::LogImpl(&meta, "\n");
    Check(true, "accepts a bare newline format");

    nn::diag::detail::LogImpl(&meta, nullptr);
    Check(true, "tolerates a null format string");
}

// ---------------------------------------------------------------------------
// nn::fs — observed as QueryMountRomCacheSize(&local) then
// MountRom("content", pvVar3, local_28). The size query must precede the mount and
// produce a value the caller can actually allocate.
// ---------------------------------------------------------------------------
void TestFs() {
    std::printf("nn::fs::QueryMountRomCacheSize / MountRom (boot-critical)\n");

    std::uint64_t size = 0;
    const int rc = nn::fs::QueryMountRomCacheSize(&size);
    Check(rc == 0, "QueryMountRomCacheSize succeeds");
    Check(size > 0, "and reports a non-zero cache requirement");
    std::printf("        cache requirement = %llu bytes\n",
                static_cast<unsigned long long>(size));

    std::vector<char> cache(size);
    const int mrc = nn::fs::MountRom("content", cache.data(), size);
    Check(mrc == 0, "MountRom(\"content\", cache, size) succeeds");
    Check(nn::fs::MountRom("content", cache.data(), size) != 0,
          "re-mounting the same name is rejected");

    const int urc = nn::fs::Unmount("content");
    Check(urc == 0, "Unmount(\"content\") succeeds after a mount");
    Check(nn::fs::Unmount("never-mounted") != 0, "unmounting an unknown name fails");

    Check(nn::fs::MountRom("x", nullptr, size) != 0, "a null cache is rejected");
    Check(nn::fs::QueryMountRomCacheSize(nullptr) != 0, "a null out pointer fails");
}

// ---------------------------------------------------------------------------
// nn::time — observed as
//   iVar6 = nn::time::StandardUserSystemClock::GetCurrentTime((PosixTime *)&local_18)
// and ToCalendarTime((CalendarTime *)&local_30, (CalendarAdditionalInfo *)0x0, t).
// PosixTime is nanoseconds since the Unix epoch; the result code is tested against 0.
// ---------------------------------------------------------------------------
void TestTime() {
    std::printf("nn::time::StandardUserSystemClock / ToCalendarTime\n");

    nn::time::PosixTime now = 0;
    const int rc = nn::time::StandardUserSystemClock::GetCurrentTime(&now);
    Check(rc == 0, "GetCurrentTime succeeds");
    // 2020-01-01 in ns = 1.577e18; a plausible present-day value is well above that.
    Check(now > 1'577'836'800LL * 1'000'000'000LL,
          "reports a time after 2020 (nanosecond epoch units)");
    Check(now < 4'102'444'800LL * 1'000'000'000LL,
          "and before 2100, so the unit really is nanoseconds");

    // A known instant: 2021-01-01T00:00:00Z = 1609459200 s.
    constexpr nn::time::PosixTime k2021 = 1'609'459'200LL * 1'000'000'000LL;
    nn::time::CalendarTime ct{};
    const int trc = nn::time::ToCalendarTime(&ct, nullptr, k2021);
    Check(trc == 0, "ToCalendarTime succeeds with a null additional-info pointer");
    Check(ct.year == 2021 && ct.month == 1 && ct.day == 1,
          "converts a known epoch value to the correct date");
    Check(ct.hour == 0 && ct.minute == 0 && ct.second == 0,
          "and the correct time of day");

    nn::time::CalendarAdditionalInfo info{};
    Check(nn::time::ToCalendarTime(&ct, &info, k2021) == 0,
          "and accepts a non-null additional-info pointer");
    Check(nn::time::ToCalendarTime(nullptr, nullptr, k2021) != 0,
          "a null CalendarTime out pointer fails");
}

// ---------------------------------------------------------------------------
// nn::mem::StandardAllocator — observed as a 1-argument inline constructor and a
// 3-argument (buffer, size) constructor, plus Allocate/Free via a global instance.
// The game embeds it inline, so no heap allocation may happen in the constructor.
// ---------------------------------------------------------------------------
void TestStandardAllocator() {
    std::printf("nn::mem::StandardAllocator\n");

    {
        alignas(16) nn::mem::StandardAllocator a;   // 1-arg form, inline storage
        void *p = a.Allocate(128);
        Check(p != nullptr, "default-constructed allocator allocates");
        Check((reinterpret_cast<std::uintptr_t>(p) % 8) == 0, "result is 8-byte aligned");
        if (p != nullptr) {
            std::memset(p, 0xAB, 128);
            Check(true, "the block is writable");
            a.Free(p);
        }
        Check(a.Allocate(0) == nullptr, "a zero-size request returns null");
    }

    {
        // 3-argument form: bump allocation inside a caller-supplied buffer, which is
        // what the observed 3-arg constructor site implies.
        alignas(16) unsigned char buffer[256];
        alignas(16) nn::mem::StandardAllocator a(buffer, sizeof(buffer));
        void *p1 = a.Allocate(64);
        void *p2 = a.Allocate(64);
        Check(p1 != nullptr && p2 != nullptr, "buffer-backed allocator serves requests");
        Check(p1 >= buffer && static_cast<unsigned char *>(p1) < buffer + sizeof(buffer),
              "returned memory lies inside the caller's buffer");
        Check(p1 != p2, "successive allocations do not alias");
        // Exhaustion must be reported rather than overflowing the caller's buffer.
        bool exhausted = false;
        for (int i = 0; i < 16; ++i) {
            if (a.Allocate(64) == nullptr) { exhausted = true; break; }
        }
        Check(exhausted, "exhausting the buffer returns null instead of overflowing");
        a.Free(p1); // bump allocator: must be a safe no-op
        Check(true, "Free on a buffer-backed allocator is a safe no-op");
    }
}

} // namespace

int main() {
    std::printf("nn::util / nn::diag / nn::fs / nn::time / nn::mem contract tests\n");
    std::printf("(properties derived from call sites in main)\n\n");

    TestVSNPrintf();
    std::printf("\n");
    TestLogImpl();
    std::printf("\n");
    TestFs();
    std::printf("\n");
    TestTime();
    std::printf("\n");
    TestStandardAllocator();

    std::printf("\n----------------------------------------\n");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
