// Lp::Utl — the first reconstructed ENGINE functions.
//
// Both are on the boot root's direct path and are ported from their decompiled bodies:
//
//   Lp::Utl::dumpDateTime(char const *)   @ 0x71012cdbf4, 216 bytes
//   Lp::Utl::dumpMemoryInfo(char const *) @ 0x71012cdccc, 408 bytes
//
// The bodies embed Nintendo's own source path:
//   D:/home/Project/Blitz/App/Lib/LunchPack2/Program/Utl/UtlSystem.cpp
// which is preserved in the log metadata below, because the binary logs it and the
// reconstructed version should be recognisably the same output.
#pragma once

namespace Lp::Utl {

// `nn::os::GetSystemTick()` is recorded by the boot root before this is called, so the
// timestamp is meaningful relative to application start.
void dumpDateTime(const char *label);

// Calls nn::os::QueryMemoryInfo and logs the result. This is the first place where the
// reconstructed engine layer and the reconstructed platform layer meet.
void dumpMemoryInfo(const char *label);

// 0x71012cdf50, 48 bytes. Sleeps the calling thread for `ms` milliseconds.
// Called by RootTask::waitDoneParallelInitThread in its isDone() spin loop.
void sleepCurThreadMilliSec(long ms);

} // namespace Lp::Utl
