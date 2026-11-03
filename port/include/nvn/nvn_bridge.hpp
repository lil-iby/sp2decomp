#pragma once
// nvn_bridge.hpp — translation layer between NVN API calls (as the game issues
// them) and the host graphics backend.
//
// Design:
//   The recompiled game calls nvnLoadCProcs, which fills a table of function
//   pointers (nvn.hpp / nvnProcs).  Each slot normally points into a real NVN
//   driver.  On PC we redirect every slot to a thin C function in this layer
//   that translates NVN concepts to Vulkan (or, during bringup, to a no-op stub
//   that logs what the game tried to do).
//
// Bringup stages (controlled by NVN_BACKEND_* macros set in CMake):
//
//   NVN_BACKEND_STUB (default):
//     Every function is a no-op or returns a safe sentinel.  Lets the boot
//     sequence run without a Vulkan driver installed.
//
//   NVN_BACKEND_VULKAN (future):
//     Each function maps to the Vulkan equivalent via the VkNvnDevice wrapper
//     defined below.  Enable by linking Vulkan and compiling with
//     -DNVN_BACKEND_VULKAN.
//
// The file deliberately contains no Vulkan headers here so that translation
// units that only need the NVN bridge do not pull in the entire Vulkan SDK.
// The implementation (nvn_bridge.cpp) includes Vulkan conditionally.
#include <cstdint>
#include <cstddef>

namespace nvn::bridge {

// ---- lifecycle -----------------------------------------------------------

// Called once by the harness / platform init before nvnLoadCProcs runs.
// On stub backend this is a no-op.
// On Vulkan backend this creates the VkInstance + VkDevice.
void Initialize();
void Finalize();

// ---- backend query -------------------------------------------------------

enum class Backend : std::uint8_t {
    Stub   = 0,  // safe no-ops; used during bringup
    Vulkan = 1,  // real rendering via Vulkan
};

Backend GetActiveBackend();
const char* GetActiveBackendName();

// ---- NVN dispatch table injection ----------------------------------------
// Call after nvnLoadCProcs fills the table.  On Stub backend this replaces
// every non-null proc with a thin logger that records the call.  On Vulkan
// backend it replaces each proc with the real Vulkan-backed implementation.
//
// Signature matches the resolver pattern in nvn.hpp:
//   typedef void (*Proc)(void);
//   extern Proc gProcs[];
void InjectProcs(void** procTable, std::size_t count);

// ---- surface / swapchain -------------------------------------------------

struct SurfaceDesc {
    void*         nativeWindowHandle; // HWND on Windows, xcb_window_t on Linux
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t backbufferCount;    // typically 2 or 3
    bool          vsync;
};

// Must be called AFTER Initialize() and BEFORE the first NVN Present call.
// On Stub this records the desc; on Vulkan this creates the VkSwapchainKHR.
bool CreateSurface(const SurfaceDesc& desc);
void DestroySurface();

// True when frames actually reach a window: the Vulkan backend found a driver and built a
// swapchain. False on the stub backend, or when Vulkan was unavailable at run time.
bool IsPresenting();

// ---- frame boundary helpers ----------------------------------------------
// The harness calls these so the backend can manage frame pacing.
void BeginFrame();   // acquire backbuffer image (Vulkan: vkAcquireNextImageKHR)
void EndFrame();     // submit + present    (Vulkan: vkQueueSubmit + vkQueuePresentKHR)

// ---- debug / telemetry ---------------------------------------------------
struct Stats {
    std::uint64_t framesRendered;
    std::uint64_t drawCallsLastFrame;
    std::uint64_t nvnCallsTotal;       // incremented by the stub logger
    std::uint64_t nvnUnimplCallsTotal; // calls that hit the UnbackedProc
};
Stats GetStats();
void  ResetStats();

} // namespace nvn::bridge
