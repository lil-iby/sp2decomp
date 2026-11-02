// nn::hid — host stub. All pads report no input.
//
// To wire real input on Windows:
//   1. Add #include <windows.h> in the _WIN32 block below.
//   2. In PollHostInput() read GetAsyncKeyState() or XInput and fill g_hostState.
// To wire SDL2 or GLFW: do the same in the cross-platform block.
#include "nn/hid.hpp"

#include <atomic>
#include <cstring>

namespace nn::hid {

namespace {

// Monotonically increasing frame counter used as samplingNumber.
std::atomic<std::uint64_t> g_frame{1};

// Global input state updated by PollHostInput() before each GetNpadStates call.
struct HostState {
    std::uint64_t buttons   = 0;
    std::int32_t  stickLX   = 0;
    std::int32_t  stickLY   = 0;
    std::int32_t  stickRX   = 0;
    std::int32_t  stickRY   = 0;
};
HostState g_hostState;

void FillState(NpadBaseState* s, std::uint64_t sampling) {
    s->samplingNumber     = sampling;
    s->buttons            = g_hostState.buttons;
    s->analogStickL.x     = g_hostState.stickLX;
    s->analogStickL.y     = g_hostState.stickLY;
    s->analogStickR.x     = g_hostState.stickRX;
    s->analogStickR.y     = g_hostState.stickRY;
    s->attributes         = 0x03; // connected + wired
}

} // namespace

// ---- lifecycle -----------------------------------------------------------

void InitializeNpad() {}
void SetSupportedNpadStyleSet(NpadStyleSet) {}
void SetSupportedNpadIdType(const NpadId*, std::size_t) {}
void SetNpadJoyAssignmentModeSingleByDefault() {}

// ---- polling -------------------------------------------------------------

static int FillStates(NpadBaseState* out, int count, NpadId id) {
    if (out == nullptr || count <= 0) return 0;
    if (id != NpadId::No1 && id != NpadId::Handheld) {
        std::memset(out, 0, sizeof(NpadBaseState));
        return 0;
    }
    PollHostInput();
    FillState(out, g_frame.fetch_add(1));
    return 1;
}

int GetNpadStates(NpadHandheldState* out, int count, NpadId id) {
    return FillStates(reinterpret_cast<NpadBaseState*>(out), count, id);
}
int GetNpadStates(NpadFullKeyState* out, int count, NpadId id) {
    return FillStates(reinterpret_cast<NpadBaseState*>(out), count, id);
}
int GetNpadStates(NpadJoyDualState* out, int count, NpadId id) {
    return FillStates(reinterpret_cast<NpadBaseState*>(out), count, id);
}

NpadStyleSet GetNpadStyle(NpadId id) {
    if (id == NpadId::No1 || id == NpadId::Handheld)
        return NpadStyleTag_NpadHandheld | NpadStyleTag_NpadFullKey;
    return 0;
}

// ---- keyboard ------------------------------------------------------------

void InitializeKeyboard() {}

int GetKeyboardStates(KeyboardState* out, int count) {
    if (out == nullptr || count <= 0) return 0;
    std::memset(out, 0, sizeof(KeyboardState));
    out->samplingNumber = g_frame.load();
    return 1;
}

// ---- host hook -----------------------------------------------------------
// Default: no input. Replace this body to wire a real input source.

void PollHostInput() {
    // No-op stub. On Windows you could do:
    //   g_hostState.buttons = 0;
    //   if (GetAsyncKeyState('A') & 0x8000) g_hostState.buttons |= NpadButton_A;
    //   ...etc
}

} // namespace nn::hid
