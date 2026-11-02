// sead::GameFrameworkNx / PerformanceMgrNx — the boot root's steps 5 and 8.
#include "sead/game_framework_nx.hpp"

#include "sead/game_framework.hpp"
#include "nn/vi.hpp"
#include "nvn/nvn.hpp"
#include "nvn/nvn_bridge.hpp"

namespace sead {

namespace {
int gPerfInitCount = 0;
} // namespace

int PerformanceMgrNx::initializeCount() { return gPerfInitCount; }

void PerformanceMgrNx::initialize() {
    // The binary's whole body is `(*(code *)PTR_Initialize_7102f9a698)();` -- a call through a
    // platform GOT slot. The slot's target is a platform entry (the name is not recoverable
    // from this call site), so the port records the invocation rather than inventing a
    // performance policy.
    //
    // This is the platform boundary again, exactly as nn::os was: a sead class forwarding to a
    // platform import. When the platform layer gains that entry, this is where it plugs in.
    ++gPerfInitCount;
}

void GameFrameworkNx::initialize(const InitializeArg &arg) {
    //   GameFramework::initialize(param_1);
    GameFramework::initialize(arg);
    //   PerformanceMgrNx::initialize();
    PerformanceMgrNx::initialize();
}

void GameFrameworkNx::initializeGraphicsSystem() {
    // Decompiled body (0x7100e6738c): six nn::vi calls that set up the display and
    // layer, then nvn::LoadCProcs to fill the 453-entry NVN proc table.
    // Order is the exact sequence from the decompiled body (and from vi_tests.cpp,
    // which replays it as a contract test).
    nn::vi::Initialize();

    nn::vi::Display *display = nullptr;
    if (nn::vi::OpenDefaultDisplay(&display) != 0 || display == nullptr) return;

    nn::vi::Layer *layer = nullptr;
    if (nn::vi::CreateLayer(&layer, display) != 0 || layer == nullptr) return;

    nn::vi::SetLayerScalingMode(layer, nn::vi::ScalingMode::PreserveAspectRatio);

    nn::os::SystemEventType vsync{};
    nn::vi::GetDisplayVsyncEvent(&vsync, display);

    nn::vi::NativeWindowHandle window = nullptr;
    nn::vi::GetNativeWindow(&window, layer);

    // Fill the NVN proc table with unbacked stubs. The real resolver (0x7100ddd6e4) uses
    // the driver's getProcAddress; we substitute one that returns the unbackedCallCount
    // function pointer so every slot is non-null and reports being invoked without
    // crashing. A real Vulkan implementation will replace these slots.
    nvn::LoadCProcs(nullptr, [](void *, const char *) -> void * {
        return reinterpret_cast<void *>(&nvn::unbackedCallCount);
    });
    nvn::bridge::Initialize();

    // Hand the native window to the bridge so the Vulkan backend can create a swapchain.
    // On the stub backend this is a no-op (CreateSurface just records the descriptor).
    // Resolution matches the Switch's standard output: 1280x720.
    nvn::bridge::SurfaceDesc surfDesc{};
    surfDesc.nativeWindowHandle = window;
    surfDesc.width              = 1280;
    surfDesc.height             = 720;
    surfDesc.backbufferCount    = 2;
    surfDesc.vsync              = true;
    nvn::bridge::CreateSurface(surfDesc);
}

} // namespace sead
