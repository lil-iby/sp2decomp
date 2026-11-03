// sead::TickSpan — a duration counted in system ticks (19.2 MHz on the Switch).
// Only the parts sead::Event and sead::Thread::sleep need.
#pragma once

#include "nn/os.hpp"

#include <cstdint>

namespace sead {

class TickSpan {
public:
    constexpr TickSpan() = default;
    constexpr explicit TickSpan(std::int64_t ticks) : mSpan(ticks) {}

    static constexpr TickSpan fromMilliSeconds(std::int64_t ms) {
        return TickSpan(ms * static_cast<std::int64_t>(nn::os::kTickFrequencyHz) / 1000);
    }

    constexpr std::int64_t toS64() const { return mSpan; }

private:
    std::int64_t mSpan = 0;
};

} // namespace sead
