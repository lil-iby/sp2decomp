// Cross-platform execution harness for recompiled Splatoon 2 guest functions.
//
// What it does:
//   1. Maps 64 MB at the guest base address (0x7100000000) so the recompiled code
//      can reach into it without page faulting on its own data references.
//   2. Provides a return trap: the guest's LR is set to guest_return_trap so a
//      `ret` (= br x30) lands in C++ instead of crashing into zeroed memory.
//   3. Catches access violations / segfaults so a fault reports the address and
//      unwinds cleanly instead of killing the process.
//
// Platform differences are contained in two regions marked PLATFORM BLOCK below.
// Everything else is shared.
//
// To use: link a generated translation unit that defines main_00000024(nx::CPU&)
// (or whichever function you want to test) and build with harness/CMakeLists.txt.
#include "nx/cpu.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csetjmp>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
// longjmp destination for the SEH handler (signal-safe version not needed on Windows).
static jmp_buf g_env;
#else
#  include <sys/mman.h>
#  include <unistd.h>
#  include <csignal>
// sigsetjmp/siglongjmp so signal masks are restored on unwind.
static sigjmp_buf g_env;
#endif

// ---- constants -----------------------------------------------------------
static constexpr std::uint64_t kMainBase = 0x7100000000ULL;
static constexpr std::uint64_t kMainSize = 0x4000000ULL; // 64 MB

// ---- guest return trap ---------------------------------------------------
// LR is set to this function. When the guest executes `ret` (br x30) control
// lands here, which longjmps back to main() with code 1.
extern "C" void guest_return_trap(nx::CPU* c) {
    (void)c;
#if defined(_WIN32)
    longjmp(g_env, 1);
#else
    siglongjmp(g_env, 1);
#endif
}

// ---- PLATFORM BLOCK: memory mapping -------------------------------------
static bool map_guest_memory() {
#if defined(_WIN32)
    void* p = VirtualAlloc(
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(kMainBase)),
        static_cast<SIZE_T>(kMainSize),
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE
    );
    if (p == nullptr) {
        std::fprintf(stderr, "harness: VirtualAlloc(0x%llx) failed — GLE %lu\n",
                     static_cast<unsigned long long>(kMainBase),
                     static_cast<unsigned long>(GetLastError()));
        return false;
    }
#else
    void* p = mmap(
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(kMainBase)),
        static_cast<std::size_t>(kMainSize),
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0
    );
    if (p == MAP_FAILED) {
        std::perror("harness: mmap");
        return false;
    }
#endif
    std::memset(p, 0, static_cast<std::size_t>(kMainSize));
    return true;
}

// ---- PLATFORM BLOCK: exception / signal handling ------------------------
#if defined(_WIN32)
static LONG WINAPI seh_handler(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    const void* addr = ep->ExceptionRecord->ExceptionInformation[1]
                       ? reinterpret_cast<void*>(ep->ExceptionRecord->ExceptionInformation[1])
                       : nullptr;
    const char* kind = (code == EXCEPTION_ACCESS_VIOLATION) ? "ACCESS_VIOLATION"
                     : (code == EXCEPTION_ILLEGAL_INSTRUCTION) ? "ILLEGAL_INSTRUCTION"
                     : "EXCEPTION";
    std::printf("    !! %s at address %p (code 0x%08lx)\n", kind, addr, (unsigned long)code);
    longjmp(g_env, 2);
}

static void install_exception_handler() {
    SetUnhandledExceptionFilter(seh_handler);
}

#else

static void signal_handler(int sig, siginfo_t* si, void*) {
    const char* name = (sig == SIGSEGV) ? "SIGSEGV"
                     : (sig == SIGILL)  ? "SIGILL"
                     : (sig == SIGBUS)  ? "SIGBUS" : "SIGNAL";
    std::printf("    !! %s at address %p\n", name, si->si_addr);
    siglongjmp(g_env, 2);
}

static void install_exception_handler() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = signal_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
}
#endif

// ---- nx:: stub implementations -------------------------------------------
// These are linked into the harness when no real translation unit is present.
// A real recompiled TU overrides them by providing the same symbols.

namespace nx {

static int g_callCount = 0;

void call(CPU& c, std::uint64_t target) {
    if (g_callCount < 6)
        std::printf("    nx::call -> 0x%llx (lr=0x%llx)\n",
                    (unsigned long long)target, (unsigned long long)c.x[30]);
    else if (g_callCount == 6)
        std::puts("    ... further nx::call output suppressed");
    ++g_callCount;
}

[[noreturn]] void unimpl(CPU& c, std::uint64_t pc, const char* what) {
    std::printf("    nx::unimpl at 0x%llx: %s\n",
                (unsigned long long)pc, what ? what : "?");
    (void)c;
#if defined(_WIN32)
    longjmp(g_env, 3);
#else
    siglongjmp(g_env, 3);
#endif
}

void svc(CPU& c, std::uint32_t imm, std::uint64_t pc) {
    if (imm != 0) std::printf("    nx::svc #%u at 0x%llx\n", imm, (unsigned long long)pc);
    (void)c;
}

std::uint64_t mrs(CPU&, std::uint32_t sysreg) {
    // TPIDR_EL0 (0xDE82) is used for TLS; return 0 as a placeholder.
    (void)sysreg;
    return 0;
}

void msr(CPU&, std::uint32_t, std::uint64_t) {}
void trace_sp(CPU&, std::uint64_t) {}

int call_count() { return g_callCount; }

} // namespace nx

// ---- guest entry point ---------------------------------------------------
// Default stub so the harness links without any real recompiled TU.
// To test real game code: compile your recompiled .cpp and pass it via
//   cmake -DSP2_GUEST_SOURCES="path/to/fn.cpp"
// The linker will prefer the real definition over this one (ODR: one TU wins).
void main_00000024(nx::CPU& c) {
    (void)c;
    std::puts("    main_00000024: no recompiled TU linked — returning immediately");
}

// ---- main ----------------------------------------------------------------

// Runs the guest; returns 0 for a normal return, else the code the handlers longjmp with
// (1 LR trap, 2 fault, 3 unimplemented). Leaving the guest by longjmp skips destructors in
// guest frames on purpose: a faulted guest has nothing worth unwinding. MSVC flags that as
// C4611; this frame owns no destructible objects, so the warning is silenced here only.
static int run_guest(nx::CPU &c) {
    int r;
#if defined(_WIN32)
#  if defined(_MSC_VER)
#    pragma warning(push)
#    pragma warning(disable : 4611)
#  endif
    r = setjmp(g_env);
#  if defined(_MSC_VER)
#    pragma warning(pop)
#  endif
#else
    r = sigsetjmp(g_env, 1);
#endif
    if (r == 0) main_00000024(c);
    return r;
}

int main() {
    std::puts("=== sp2decomp execution harness ===\n");

    if (!map_guest_memory()) return 1;
    std::printf("guest memory mapped at 0x%llx (%llu MB)\n\n",
                (unsigned long long)kMainBase,
                (unsigned long long)(kMainSize >> 20));

    install_exception_handler();

    nx::CPU c;
    std::memset(&c, 0, sizeof(c));

    const std::uint64_t stack_top = kMainBase + 0x3000000ULL;
    c.x[31] = stack_top;
    c.x[30] = reinterpret_cast<std::uint64_t>(&guest_return_trap);
    c.x[0]  = reinterpret_cast<std::uint64_t>(&c);

    std::printf("SP = 0x%llx   LR = %p (return trap)\n",
                (unsigned long long)c.x[31], (void*)c.x[30]);
    std::printf("calling main_00000024 ...\n\n");
    std::fflush(stdout);

    const int r = run_guest(c);

    if (r == 0) {
        std::puts("    guest function returned normally (C++ return for `ret`)");
    } else if (r == 1) {
        std::puts("    GUEST RETURNED via the LR trap");
    } else if (r == 2) {
        std::puts("    FAULTED — see the address above");
    } else {
        std::puts("    STOPPED at an nx::unimpl site");
    }

    std::printf("\n--- guest state after the call ---\n");
    std::printf("  SP   = 0x%llx  (initial 0x%llx)\n",
                (unsigned long long)c.x[31], (unsigned long long)stack_top);
    std::printf("  x19  = 0x%llx\n", (unsigned long long)c.x[19]);
    std::printf("  x8   = 0x%llx\n", (unsigned long long)c.x[8]);
    std::printf("  nzcv = n%d z%d c%d v%d\n", (int)c.nf, (int)c.zf, (int)c.cf, (int)c.vf);
    std::printf("  nx::call invocations: %d\n", nx::call_count());
    std::puts("\n*** harness finished ***");
    return (r == 0 || r == 1) ? 0 : 1;
}
