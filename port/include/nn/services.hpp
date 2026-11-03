// nn::time / nn::mem — platform services reconstructed from call-site evidence.
// (nn::fs lives in nn/fs.hpp.)
#pragma once

#include <cstdint>
#include <cstddef>

namespace nn::time {

// A PosixTime is nanoseconds since the Unix epoch (SDK convention).
using PosixTime = std::int64_t;

// `iVar6 = nn::time::StandardUserSystemClock::GetCurrentTime((PosixTime *)&local_18);`
struct CalendarTime {
    std::uint16_t year;
    std::uint8_t month;
    std::uint8_t day;
    std::uint8_t hour;
    std::uint8_t minute;
    std::uint8_t second;
    std::uint8_t padding;
};

struct CalendarAdditionalInfo {
    std::uint32_t dayOfWeek;
    std::uint32_t dayOfYear;
    std::int64_t gmtOffset;
    char name[8];
};

struct StandardUserSystemClock {
    static int GetCurrentTime(PosixTime *outTime);
    static int SetCurrentTime(PosixTime time);
};

// `nn::time::ToCalendarTime((CalendarTime *)&local_30, (CalendarAdditionalInfo *)0x0, aPStack_28);`
// -> int result; the additional-info argument may be null at observed call sites.
int ToCalendarTime(CalendarTime *outTime, CalendarAdditionalInfo *outInfo, PosixTime time);

} // namespace nn::time

namespace nn::mem {

// `nn::mem::StandardAllocator::StandardAllocator((StandardAllocator *)(this + 0x230));`
// and a 3-argument form taking a backing buffer and size. The game embeds these inline
// in its own objects, so the storage is caller-owned.
//
// SIZE: not established by evidence. The game only ever holds it inline as a field, and
// no adjacent sibling field appears in the observed call sites, so no bound is derivable
// (unlike MessageQueueType, see docs/TYPE_SIZES.md).
class StandardAllocator {
public:
    StandardAllocator();
    StandardAllocator(void *buffer, std::size_t size);
    ~StandardAllocator();

    void *Allocate(std::size_t size, std::size_t alignment = 8);
    void *Allocate(std::size_t size, void (*onAllocate)(void *), void *user);
    void Free(void *ptr);

private:
    // Backed by a host allocator. The game never reads these fields directly.
    struct Impl;
    alignas(16) unsigned char mStorage[0x40];
};

} // namespace nn::mem
