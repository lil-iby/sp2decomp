// nn::oe — Operation Environment stubs.
// Always in-focus, always docked, no home-menu messages.
#include "nn/oe.hpp"

#include <atomic>

namespace nn::oe {

namespace {
std::atomic<bool> g_exitRequested{false};
} // namespace

void Initialize() {}
void Finalize()   {}

FocusState     GetCurrentFocusState() { return FocusState::InFocus; }
OperationMode  GetOperationMode()     { return OperationMode::Docked; }
PerformanceMode GetPerformanceMode()  { return PerformanceMode::Boost; }

bool TryPopMessage(Message* outMessage) {
    if (g_exitRequested.load()) {
        if (outMessage != nullptr) *outMessage = Message::ExitRequested;
        return true;
    }
    if (outMessage != nullptr) *outMessage = Message::None;
    return false;
}

void FinishStartupLogo()                  {}
void SetRecommendedMasterVolume(float)     {}

bool IsExitRequested()             { return g_exitRequested.load(); }
void SetExitRequested(bool value)  { g_exitRequested.store(value); }

} // namespace nn::oe
