// nx::CPU — AArch64 register state model for recompiled guest functions.
//
// Every function the Ghidra recompiler emits has the signature:
//     void main_XXXXXXXX(nx::CPU& c)
// and manipulates registers by reading/writing c.x[N], c.nf, etc.
// This header defines that contract and the six out-of-line entry points
// the generated code calls for indirect branches, SVCs and system registers.
//
// Floating-point / NEON (v0-v31, fpcr, fpsr) is omitted until a generated
// function actually uses it; extend the struct here and update the impls.
#pragma once

#include <cstdint>

namespace nx {

// AArch64 register file.
// x[0..30]  general purpose
// x[31]     SP (stack pointer — the AArch64 convention)
// nf/zf/cf/vf  condition flags
struct CPU {
    std::uint64_t x[32] = {};
    bool nf = false;
    bool zf = false;
    bool cf = false;
    bool vf = false;
};

// Called when the generated code encounters a branch-via-register whose target
// is not statically resolvable at recompilation time (e.g. `blr x8`).
void call(CPU& c, std::uint64_t target);

// Called for any instruction the recompiler does not yet translate. Reports
// the PC and a description, then terminates the guest session via longjmp.
[[noreturn]] void unimpl(CPU& c, std::uint64_t pc, const char* what);

// Called for SVC instructions (Nintendo's syscall ABI).
void svc(CPU& c, std::uint32_t imm, std::uint64_t pc);

// MRS — read a system register (TPIDR_EL0, CNTVCT_EL0, etc.).
std::uint64_t mrs(CPU& c, std::uint32_t sysreg);

// MSR — write a system register.
void msr(CPU& c, std::uint32_t sysreg, std::uint64_t value);

// Debug hook: called with every SP write so a trace can log stack depth.
void trace_sp(CPU& c, std::uint64_t pc);

} // namespace nx
