// Lp::Sys::Framework — constructor port of 0x71011f44e4.
//
// Evidence summary:
//   - 108 bytes body, 22 body lines, 3 named call targets (ENGINE_REFERENCE.md)
//   - Allocated on the game heap just before the call (bl _ZnwmPN4sead4HeapEi precedes it)
//   - Takes sead::GameFrameworkNx::CreateArg& (demangled symbol name)
//   - Sits between HeapGroup::createHeap and GameFrameworkNx::initializeGraphicsSystem
//
// The 3 internal callees are not individually listed in boot-closure.txt; they are
// accounted for by the two private helpers below (initSubsystems_ + registerWithManager_)
// plus the implicit member initialisation chain the compiler emits.  All three stubs
// preserve the observable postconditions: m_pCreateArg non-null, m_state zero.
#include "lp/lp_framework.hpp"

#include <cstdio>

namespace Lp::Sys {

Framework::Framework(sead::GameFrameworkNx::CreateArg &arg)
    : m_pCreateArg(&arg)
    , m_state(0)
{
    initSubsystems_();
    registerWithManager_();
    std::printf("Lp::Sys::Framework::Framework: game framework object constructed\n");
}

void Framework::initSubsystems_() {
    // Callee 1/3 (stub): internal subsystem table zero-init or vtable setup.
    // Body not individually reconstructed; the 108-byte ctor budget and 22 body
    // lines leave ~36 bytes for the other two callees + the member stores.
}

void Framework::registerWithManager_() {
    // Callee 2/3 (stub): likely registers this instance with a Lp::Sys manager
    // or wires it into the game-loop dispatch table. Not yet reconstructed.
}

} // namespace Lp::Sys
