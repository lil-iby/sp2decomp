// sead::GameFrameworkNx — the boot root's step 5, ported from 0x7100e6724c (20 bytes).
//
// The decompiled body forwards to two things:
//
//   void sead::GameFrameworkNx::initialize(const Framework::InitializeArg& arg) {
//       GameFramework::initialize(arg);      // 0x7100e69fe4, 456 bytes
//       PerformanceMgrNx::initialize();      // 0x7100e68c44 -- a 4-byte PLATFORM import
//   }
//
// PerformanceMgrNx::initialize's own body is a single call through a platform GOT slot:
//   (*(code *)PTR_Initialize_7102f9a698)();
// which is nn::oe::Initialize or similar -- a platform entry the reconstructed platform layer
// must satisfy. It is modelled as a platform hook here so the forwarding wrapper can be
// ported without inventing a performance-configuration policy that the binary does not expose
// at this call site.
#pragma once

// GameFrameworkNx derives from GameFramework, so its definition is needed here -- including
// only framework_args.hpp left GameFramework incomplete and the compiler rejected the base
// class.
#include "sead/game_framework.hpp"

namespace sead {

// sead::PerformanceMgrNx — only the function the boot root reaches.
class PerformanceMgrNx {
public:
    // 0x7100e68c44, 4 bytes: forwards to the platform import PTR_Initialize_7102f9a698.
    static void initialize();

    // How many times the platform entry has been invoked. Exposed so a test can verify the
    // forward actually happened rather than the call being silently dropped.
    static int initializeCount();
};

// sead::GameFrameworkNx — the boot root's step 5.
class GameFrameworkNx : public GameFramework {
public:
    // Argument struct passed by reference to Lp::Sys::Framework::Framework
    // (demangled name: GameFrameworkNx::CreateArg&; internal fields not yet reconstructed
    // — no layout evidence in the boot closure; treated as an opaque config carrier).
    struct CreateArg {
        // Reserved for future reconstruction once internal fields are observed.
    };

    // 0x7100e6724c, 20 bytes.
    static void initialize(const InitializeArg &arg);

    // 0x7100e6738c, ~400 bytes.
    // Calls the six nn::vi display-setup functions then nvn::LoadCProcs to fill the NVN
    // proc table with stub trampolines. The exact call sequence is from the decompiled body
    // and is also tested independently in tests/vi_tests.cpp.
    static void initializeGraphicsSystem();
};

} // namespace sead
