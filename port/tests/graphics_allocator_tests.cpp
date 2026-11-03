// Contract tests for nv::SetGraphicsAllocator and nn::mem::StandardAllocator.
//
// These are the FIRST two calls sead::GameFrameworkNx::initializeGraphicsSystem makes, so they
// are the first graphics-subsystem code the port exercises.
#include "nv/graphics_allocator.hpp"
#include "nn/vi.hpp"
#include "nn/services.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int g_failures = 0;
int g_checks = 0;
void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else { std::printf("  ok    %s\n", what); }
}
} // namespace

// The three callbacks the binary installs, reimplemented over host memory. The real bodies are
// LAB_7100e67cd0 / LAB_7100e67ce0 / LAB_7100e67ce4 in the decompile.
namespace {
void *HostAlloc(std::uint64_t size, std::uint64_t alignment, void *user) {
    (void)user;
    if (size == 0) return nullptr;
    if (alignment < alignof(void *)) alignment = alignof(void *);
    void *p = nullptr;
#if defined(_WIN32)
    p = _aligned_malloc(static_cast<std::size_t>(size), static_cast<std::size_t>(alignment));
#else
    if (posix_memalign(&p, static_cast<std::size_t>(alignment),
                       static_cast<std::size_t>(size)) != 0) p = nullptr;
#endif
    return p;
}
void HostFree(void *ptr, void *user) {
    (void)user;
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}
void *HostRealloc(void *ptr, std::uint64_t size, std::uint64_t alignment, void *user) {
    (void)user;
    if (ptr == nullptr) return HostAlloc(size, alignment, user);
    if (size == 0) { HostFree(ptr, user); return nullptr; }
    // Host allocations are not resizable in place, so allocate/copy/free.
    void *p = HostAlloc(size, alignment, user);
    if (p != nullptr) {
        std::memcpy(p, ptr, static_cast<std::size_t>(size));
        HostFree(ptr, user);
    }
    return p;
}
} // namespace

int main() {
    std::printf("nv::SetGraphicsAllocator + nn::mem::StandardAllocator contract tests\n");
    std::printf("(the first two calls of initializeGraphicsSystem, 0x7100e6738c)\n\n");

    nv::ResetForTesting();

    std::printf("nv::SetGraphicsAllocator (0x7101413cb8, four parameters)\n");
    Check(nv::currentAlloc() == nullptr, "no allocator is installed initially");
    Check(nv::AllocViaGraphicsAllocator(64, 8) == nullptr,
          "and allocating before install returns null rather than faulting");

    nv::SetGraphicsAllocator(&HostAlloc, &HostFree, &HostRealloc, nullptr);
    Check(nv::currentAlloc() == &HostAlloc, "the alloc callback is installed");
    Check(nv::currentFree() == &HostFree, "the free callback is installed");
    Check(nv::currentRealloc() == &HostRealloc, "the realloc callback is installed");
    Check(nv::currentUser() == nullptr, "user data is stored as passed (null here)");

    std::printf("\nthe installed allocator is actually reachable\n");
    {
        const int before = nv::allocatorCallCount();
        void *p = nv::AllocViaGraphicsAllocator(0x1000, 0x1000);
        Check(p != nullptr, "an allocation through the installed allocator succeeds");
        Check(nv::allocatorCallCount() == before + 1, "and the call is counted");
        Check(p != nullptr && (reinterpret_cast<std::uintptr_t>(p) % 0x1000) == 0,
              "the requested 0x1000 alignment is honoured");
        HostFree(p, nullptr);
    }

    std::printf("\nthe allocator survives reinstallation (as the engine may re-run bring-up)\n");
    {
        nv::SetGraphicsAllocator(&HostAlloc, &HostFree, &HostRealloc,
                                 reinterpret_cast<void *>(0xabc));
        Check(nv::currentUser() == reinterpret_cast<void *>(0xabc),
              "a second install replaces the user data");
        void *p = nv::AllocViaGraphicsAllocator(32, 8);
        Check(p != nullptr, "and the new installation still allocates");
        HostFree(p, nullptr);
    }

    std::printf("\nnn::mem::StandardAllocator (0x7101413038) -- ALREADY in the platform layer\n");
    {
        // NOTE: this class already existed in nn/services.hpp. The first version of this test
        // assumed a different API (Deallocate/allocatedSize); the linker caught the duplicate
        // definition and reading services.hpp gave the real method names.
        nn::mem::StandardAllocator alloc;
        void *a = alloc.Allocate(0x200, 16);
        Check(a != nullptr, "Allocate returns memory");
        // Like malloc, the SDK allocator promises nothing about fresh contents; a zero-fill
        // check only passed when the OS happened to hand back new pages.
        auto *bytes = static_cast<unsigned char *>(a);
        for (int i = 0; i < 0x200; ++i) bytes[i] = static_cast<unsigned char>(i);
        Check(bytes[0] == 0 && bytes[0x1FF] == 0xFF, "the whole block is writable");
        Check((reinterpret_cast<std::uintptr_t>(a) % 16) == 0, "the alignment is honoured");
        alloc.Free(a);
        Check(true, "Free completes (the platform layer's method name, not Deallocate)");
        Check(alloc.Allocate(0, 8) == nullptr, "a zero-size request returns null");
    }
    std::printf("\nthe ported nn::vi layer is reachable from the same path\n");
    {
        // initializeGraphicsSystem calls the six nn::vi functions after the allocator install,
        // so this confirms the two increments compose.
        nn::vi::ResetForTesting();
        nn::vi::Initialize();
        nn::vi::Display *d = nullptr;
        Check(nn::vi::OpenDefaultDisplay(&d) == 0 && d != nullptr,
              "the display layer still works alongside the allocator");
    }

    std::printf("\n----------------------------------------\n");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
