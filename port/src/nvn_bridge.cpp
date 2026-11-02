// nvn_bridge.cpp — NVN → host-graphics bridge implementation.
//
// Stub backend (default): every proc is replaced with a thin logger.
// Vulkan backend (future): compile with -DNVN_BACKEND_VULKAN.
//
// See nvn/nvn_bridge.hpp for the design rationale.
#include "nvn/nvn_bridge.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>

// ---- optional Vulkan include ---------------------------------------------
#if defined(NVN_BACKEND_VULKAN)
#  include <vulkan/vulkan.h>
#endif

namespace nvn::bridge {

// ---- module state --------------------------------------------------------
namespace {

#if defined(NVN_BACKEND_VULKAN)
constexpr Backend kActiveBackend = Backend::Vulkan;
#else
constexpr Backend kActiveBackend = Backend::Stub;
#endif

std::atomic<std::uint64_t> g_nvnCalls{0};
std::atomic<std::uint64_t> g_nvnUnimpl{0};
std::atomic<std::uint64_t> g_frames{0};
std::atomic<std::uint64_t> g_drawCalls{0};

SurfaceDesc g_surface{};
bool        g_surfaceCreated{false};

} // namespace

// ---- lifecycle -----------------------------------------------------------

void Initialize() {
#if defined(NVN_BACKEND_VULKAN)
    // TODO: vkCreateInstance + vkCreateDevice
    // Pick a physical device with VK_KHR_swapchain, VK_KHR_dynamic_rendering.
    std::puts("nvn::bridge: Vulkan backend — not yet implemented, falling back to stub");
#else
    std::puts("nvn::bridge: Stub backend active");
#endif
}

void Finalize() {
#if defined(NVN_BACKEND_VULKAN)
    // TODO: vkDeviceWaitIdle + destroy swapchain, device, surface, instance
#endif
    g_surfaceCreated = false;
}

// ---- backend query -------------------------------------------------------

Backend     GetActiveBackend()     { return kActiveBackend; }
const char* GetActiveBackendName() {
    return (kActiveBackend == Backend::Vulkan) ? "Vulkan" : "Stub";
}

// ---- proc injection ------------------------------------------------------

// Each stub proc logs the call and bumps the counter.
// The name is not available at runtime (NVN procs are indexed, not named),
// so we log the slot index instead.
struct StubContext {
    std::size_t index;
};

// We store one StubContext per possible slot (up to 512) so the function
// pointer can carry its slot index without a heap allocation.
static constexpr std::size_t kMaxProcs = 512;
static StubContext g_stubCtx[kMaxProcs];

// The actual function pointer stored in gProcs must be a plain void(*)().
// We generate one trampoline per architecture:
//   On x86-64 MSVC / GCC we use a static array of pre-built stubs that read
//   their index from g_stubCtx.  For simplicity during bringup we just store
//   a single generic function and accept losing the per-slot index in the log.
static void GenericStubProc() {
    g_nvnCalls.fetch_add(1, std::memory_order_relaxed);
}

static void UnbackedStubProc() {
    g_nvnUnimpl.fetch_add(1, std::memory_order_relaxed);
    g_nvnCalls.fetch_add(1, std::memory_order_relaxed);
}

void InjectProcs(void** procTable, std::size_t count) {
    if (procTable == nullptr || count == 0) return;
    const std::size_t n = (count < kMaxProcs) ? count : kMaxProcs;
    for (std::size_t i = 0; i < n; ++i) {
        if (procTable[i] == nullptr) {
            // Slot was never resolved — point at the unbacked stub.
            procTable[i] = reinterpret_cast<void*>(&UnbackedStubProc);
        } else {
#if defined(NVN_BACKEND_VULKAN)
            // TODO: replace each slot with the real Vulkan-backed function.
            // For now, leave the proc as-is so the Vulkan driver can handle it
            // if nvnLoadCProcs ever resolved to a real driver.
            (void)i;
#else
            // Stub backend: replace every resolved proc with our logger.
            procTable[i] = reinterpret_cast<void*>(&GenericStubProc);
#endif
        }
    }
    std::printf("nvn::bridge: injected into %zu NVN proc slots (%s backend)\n",
                n, GetActiveBackendName());
}

// ---- surface / swapchain -------------------------------------------------

bool CreateSurface(const SurfaceDesc& desc) {
    g_surface        = desc;
    g_surfaceCreated = true;
    std::printf("nvn::bridge: surface %ux%u back=%u vsync=%d (window=%p)\n",
                desc.width, desc.height, desc.backbufferCount, (int)desc.vsync,
                desc.nativeWindowHandle);
#if defined(NVN_BACKEND_VULKAN)
    // TODO: vkCreateSurfaceKHR (Win32 / Xcb) + vkCreateSwapchainKHR
#endif
    return true;
}

void DestroySurface() {
#if defined(NVN_BACKEND_VULKAN)
    // TODO: vkDestroySwapchainKHR + vkDestroySurfaceKHR
#endif
    g_surfaceCreated = false;
}

// ---- frame boundary ------------------------------------------------------

void BeginFrame() {
#if defined(NVN_BACKEND_VULKAN)
    // TODO: vkAcquireNextImageKHR
#endif
}

void EndFrame() {
    g_frames.fetch_add(1, std::memory_order_relaxed);
    g_drawCalls.store(0, std::memory_order_relaxed); // reset per-frame counter
#if defined(NVN_BACKEND_VULKAN)
    // TODO: vkQueueSubmit + vkQueuePresentKHR
#endif
}

// ---- stats ---------------------------------------------------------------

Stats GetStats() {
    Stats s;
    s.framesRendered      = g_frames.load(std::memory_order_relaxed);
    s.drawCallsLastFrame  = g_drawCalls.load(std::memory_order_relaxed);
    s.nvnCallsTotal       = g_nvnCalls.load(std::memory_order_relaxed);
    s.nvnUnimplCallsTotal = g_nvnUnimpl.load(std::memory_order_relaxed);
    return s;
}

void ResetStats() {
    g_nvnCalls.store(0,  std::memory_order_relaxed);
    g_nvnUnimpl.store(0, std::memory_order_relaxed);
    g_frames.store(0,    std::memory_order_relaxed);
    g_drawCalls.store(0, std::memory_order_relaxed);
}

} // namespace nvn::bridge
