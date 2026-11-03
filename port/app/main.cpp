// sp2port — the runnable port.
//
// Runs the reconstructed boot sequence, then keeps sead::Framework::run presenting frames
// until the window is closed. With the stub backend (or no Vulkan driver) nothing can be
// shown, so it prints the boot report and exits.
#include "lp/boot_sequence.hpp"
#include "nvn/nvn_bridge.hpp"
#include "sead/framework_args.hpp"

#include <cstdio>

int main() {
    // Unbuffered, so a log captured from a hung or killed run still shows the last step.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("sp2port (%s backend)\n\n", nvn::bridge::GetActiveBackendName());

    const lp::recon::BootRunResult boot = lp::recon::runBootSequence();
    std::printf("\n%s\n", boot.summary().c_str());
    if (!boot.reachedEnd) return 1;

    if (!nvn::bridge::IsPresenting()) {
        std::puts("No window to present to (stub backend or no Vulkan driver); exiting.");
        return 0;
    }

    std::puts("Presenting frames -- close the window to quit.");
    sead::Framework framework;
    sead::Framework::RunArg runArg;
    sead::Framework::setFrameLimit(-1);
    framework.run(runArg);

    const nvn::bridge::Stats stats = nvn::bridge::GetStats();
    std::printf("Window closed after %llu frames.\n",
                static_cast<unsigned long long>(stats.framesRendered));
    return 0;
}
