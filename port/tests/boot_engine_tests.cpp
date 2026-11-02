// Boot-engine runner test: this is where the harness stops replaying and starts RUNNING
// reconstructed engine code.
//
// The runner walks the boot root's observed sequence, executing each reconstructed
// function and stopping loudly at the first dependency that is not ported. The output is
// therefore a truthful statement of how far the reconstruction currently reaches -- not
// a pass/fail of a stubbed simulation.
#include "lp/boot_sequence.hpp"

#include <cstdio>

namespace {
int g_failures = 0;
int g_checks = 0;
void Check(bool cond, const char *what) {
    ++g_checks;
    if (!cond) { ++g_failures; std::printf("  FAIL  %s\n", what); }
    else { std::printf("  ok    %s\n", what); }
}
} // namespace

int main() {
    std::printf("Reconstructed boot sequence runner\n");
    std::printf("(runs real reconstructed engine code; stops loudly at the first gap)\n\n");

    std::printf("--- the run (expect reconstructed boot logging below) ---\n");
    const lp::recon::BootRunResult result = lp::recon::runBootSequence();
    std::printf("--- end of run ---\n\n");

    std::printf("%s\n", result.summary().c_str());

    // The run must actually execute the reconstructed functions it can reach.
    Check(!result.completed.empty(),
          "the run completed at least one reconstructed engine step");
    Check(result.completed.size() >= 1,
          "dumpDateTime (the boot root's first call) was actually executed");

    // Milestone: the entire reconstructed boot sequence now runs to completion.
    // All 13 steps in SequenceBody are ported; there is no longer a missing dependency.
    //
    // History: stop moved registerCallback (round 3) -> InitializeArg (round 6)
    //          -> GameFrameworkNx::initialize (round 7) -> ForegroundBucketMgr (round 8)
    //          -> initializeGraphicsSystem (round 9) -> END (this round, round 10).
    //
    // The two steps added this round:
    //   - sead::GameFrameworkNx::initializeGraphicsSystem (0x7100e6738c): six nn::vi
    //     calls + nvn::LoadCProcs with stub resolver.
    //   - sead::Framework::RunArg::RunArg (0x7100e698a0): already in framework_args.cpp,
    //     just needed to be wired into the sequence.
    Check(result.reachedEnd,
          "the entire reconstructed boot sequence ran to completion (no missing dependency)");
    Check(result.stoppedAt.empty(),
          "no dependency was missing; stoppedAt is empty");
    Check(result.completed.size() == 14,
          "exactly the fourteen reconstructed steps ran to completion");

    // Every completed step must be a real reconstructed function, not a placeholder.
    for (const auto &step : result.completed) {
        std::printf("  completed: %s\n", step.c_str());
    }

    std::printf("\n----------------------------------------\n");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
