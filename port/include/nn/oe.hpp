// nn::oe — Operation Environment stubs.
// Handles home-menu events, focus state, and display-mode queries.
// On PC: always in-focus, always docked, startup logo already done.
#pragma once

#include <cstdint>

namespace nn::oe {

enum class FocusState : std::uint32_t {
    InFocus    = 1,
    OutOfFocus = 2,
    Background = 3,
};

enum class OperationMode : std::uint32_t {
    Handheld = 0,
    Docked   = 1,
};

enum class PerformanceMode : std::uint32_t {
    Normal  = 0,
    Boost   = 1,
};

enum class Message : std::uint32_t {
    None               = 0,
    FocusStateChanged  = 1,
    OperationModeChanged = 2,
    PerformanceModeChanged = 3,
    ExitRequested      = 0x15,
};

// Lifecycle
void Initialize();
void Finalize();

// State queries
FocusState     GetCurrentFocusState();
OperationMode  GetOperationMode();
PerformanceMode GetPerformanceMode();

// Message pump — returns false immediately (no pending messages on PC).
bool TryPopMessage(Message* outMessage);

// Display
void FinishStartupLogo();
void SetRecommendedMasterVolume(float volume);

// Exit request (raised when the window is closed).
bool IsExitRequested();
void SetExitRequested(bool requested);

} // namespace nn::oe
