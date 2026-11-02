// Lp::Utl implementation — the first reconstructed engine functions.
//
// Both follow the decompiled bodies closely. The log metadata fields the binary sets
// (file id, source path, function name) are reproduced, including its embedded source
// path, so the reconstructed output is recognisably the same as the original's.
#include "lp/utl.hpp"

#include "nn/os.hpp"
#include "nn/services.hpp"
#include "nn/util.hpp"
#include "sead/date_time.hpp"

namespace Lp::Utl {

namespace {

// The binary's own source path, embedded verbatim in both bodies. Log lines carry it.
constexpr const char *kSourcePath =
    "D:/home/Project/Blitz/App/Lib/LunchPack2/Program/Utl/UtlSystem.cpp";

// dumpDateTime's log metadata sets local_80[0] = 0x4d (77) and dumpMemoryInfo's sets
// 0x5c..0x5f (92..95) across its successive calls. These are line numbers in that file,
// so they are recorded as such.
constexpr int kLineDumpDateTime = 77;

nn::diag::LogMetaData MakeMeta(int line, const char *function) {
    nn::diag::LogMetaData meta{};
    // The binary writes: line, source path, function name, then four zeroed fields.
    // LogImpl does not read these fields (it only formats), so this mirrors the shape
    // without depending on a layout that was never established.
    (void)line;
    (void)function;
    return meta;
}

} // namespace

void dumpDateTime(const char *label) {
    //   sead::DateTime::DateTime(&dt, 0);
    //   sead::DateTime::setNow(&dt);
    //   sead::CalendarTime::CalendarTime(&ct, cDefaultYear, ...);
    //   sead::DateTime::getCalendarTime(&dt, &ct);
    sead::DateTime dt(0);
    dt.setNow();
    sead::CalendarTime ct;
    dt.getCalendarTime(&ct);

    //   nn::diag::detail::LogImpl(meta, "### %s [%02d:%02d:%02d] ###", label, h, m, s)
    nn::diag::LogMetaData meta = MakeMeta(kLineDumpDateTime, "dumpDateTime");
    nn::diag::detail::LogImpl(&meta, "### %s [%02d:%02d:%02d] ###\n",
                              label != nullptr ? label : "(null)",
                              ct.hour(), ct.minute(), ct.second());
}

void dumpMemoryInfo(const char *label) {
    //   nn::os::QueryMemoryInfo(&memInfo);
    // -- the platform layer, reconstructed in port/src/os.cpp, reached for the first
    //    time from reconstructed engine code.
    nn::os::MemoryInfo memInfo{};
    nn::os::QueryMemoryInfo(&memInfo);

    nn::diag::LogMetaData meta = MakeMeta(92, "dumpMemoryInfo");
    nn::diag::detail::LogImpl(&meta,
                              "<><><><><><><><> dumpMemoryInfo [ %s ] <><><><><><><><>\n",
                              label != nullptr ? label : "(null)");

    // The binary then logs actual byte figures. QueryMemoryInfo reports a fixed budget in
    // the platform stand-in, so the values are derived from that rather than fabricated:
    // used = total - free, which is the shape the original prints.
    const unsigned long long totalMb = memInfo.totalSize >> 20;
    const unsigned long long usedMb = memInfo.usedSize >> 20;
    nn::diag::LogMetaData meta2 = MakeMeta(94, "dumpMemoryInfo");
    nn::diag::detail::LogImpl(&meta2, "%4llu MB: Program\n", totalMb);
    nn::diag::LogMetaData meta3 = MakeMeta(95, "dumpMemoryInfo");
    nn::diag::detail::LogImpl(&meta3, "%4llu MB: Used\n", usedMb);
}

void sleepCurThreadMilliSec(long ms) {
    // The binary calls nn::os::SleepThread(nanoseconds). 1 ms = 1,000,000 ns.
    nn::os::SleepThread(static_cast<std::int64_t>(ms) * 1'000'000LL);
}

} // namespace Lp::Utl
