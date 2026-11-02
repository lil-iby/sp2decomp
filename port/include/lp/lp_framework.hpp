#pragma once
// Lp::Sys::Framework — ported from the boot closure.
//
// Address  : 0x71011f44e4  (depth 1 in the boot callgraph)
// Body     : 108 bytes · 3 distinct call targets · 22 body lines
// Signature (demangled): Framework::Framework(sead::GameFrameworkNx::CreateArg&)
//
// Role: the game-side framework container constructed by SysInit::Init between
// HeapGroup::createHeap and GameFrameworkNx::initializeGraphicsSystem.  It is
// allocated on the game heap (the preceding `bl _ZnwmPN4sead4HeapEi` in the
// callgraph confirms heap allocation) and holds the config arg until the full
// framework boot is complete.
//
// The three call targets inside the ctor body are not yet individually resolved
// (boot-closure.txt only records the count); their stubs are inline here.
#include "sead/game_framework_nx.hpp"

#include <cstdint>

namespace Lp::Sys {

class Framework {
public:
    // 0x71011f44e4, 108 bytes.
    explicit Framework(sead::GameFrameworkNx::CreateArg &arg);

    // Accessor used in tests to verify the arg was stored.
    const sead::GameFrameworkNx::CreateArg *createArg() const { return m_pCreateArg; }

private:
    // Three internal helpers called from the ctor (targets not individually named in
    // boot-closure.txt; stubs declared private).
    void initSubsystems_();
    void registerWithManager_();

    const sead::GameFrameworkNx::CreateArg *m_pCreateArg;
    std::uint32_t m_state;  // zero-initialised by the ctor
};

} // namespace Lp::Sys
