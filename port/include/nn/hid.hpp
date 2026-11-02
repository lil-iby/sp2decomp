// nn::hid — Human Interface Devices (controllers + keyboard).
//
// Surface reconstructed from the binary's import table and the official nnsdk
// public headers (matching symbol names and argument counts).
// The PC stub maps no physical input; all buttons read as released.
// To wire real input: implement PollHostInput() in port/src/hid.cpp.
#pragma once

#include <cstdint>
#include <cstddef>

namespace nn::hid {

// ---- controller identity ------------------------------------------------

enum class NpadId : std::uint32_t {
    No1      = 0,
    No2      = 1,
    No3      = 2,
    No4      = 3,
    No5      = 4,
    No6      = 5,
    No7      = 6,
    No8      = 7,
    Handheld = 0x20,
    Invalid  = 0xFFFFFFFF,
};

enum NpadStyleTag : std::uint32_t {
    NpadStyleTag_NpadFullKey    = 1u << 0,
    NpadStyleTag_NpadJoyDual    = 1u << 1,
    NpadStyleTag_NpadJoyLeft    = 1u << 2,
    NpadStyleTag_NpadJoyRight   = 1u << 3,
    NpadStyleTag_NpadHandheld   = 1u << 5,
};
using NpadStyleSet = std::uint32_t;

// ---- button bitmask ------------------------------------------------------

enum NpadButton : std::uint64_t {
    NpadButton_A          = 1ULL << 0,
    NpadButton_B          = 1ULL << 1,
    NpadButton_X          = 1ULL << 2,
    NpadButton_Y          = 1ULL << 3,
    NpadButton_StickL     = 1ULL << 4,
    NpadButton_StickR     = 1ULL << 5,
    NpadButton_L          = 1ULL << 6,
    NpadButton_R          = 1ULL << 7,
    NpadButton_ZL         = 1ULL << 8,
    NpadButton_ZR         = 1ULL << 9,
    NpadButton_Plus       = 1ULL << 10,
    NpadButton_Minus      = 1ULL << 11,
    NpadButton_Left       = 1ULL << 12,
    NpadButton_Up         = 1ULL << 13,
    NpadButton_Right      = 1ULL << 14,
    NpadButton_Down       = 1ULL << 15,
    NpadButton_StickLLeft = 1ULL << 16,
    NpadButton_StickLUp   = 1ULL << 17,
    NpadButton_StickLRight= 1ULL << 18,
    NpadButton_StickLDown = 1ULL << 19,
    NpadButton_StickRLeft = 1ULL << 20,
    NpadButton_StickRUp   = 1ULL << 21,
    NpadButton_StickRRight= 1ULL << 22,
    NpadButton_StickRDown = 1ULL << 23,
};

// ---- state types ---------------------------------------------------------

struct AnalogStickState {
    std::int32_t x; // [-32767, 32767]
    std::int32_t y;
};

// Shared layout for all npad state types.
struct NpadBaseState {
    std::uint64_t samplingNumber;
    std::uint64_t buttons;          // bitmask of NpadButton
    AnalogStickState analogStickL;
    AnalogStickState analogStickR;
    std::uint32_t attributes;
    std::uint8_t  _pad[4];
};

using NpadFullKeyState   = NpadBaseState;
using NpadHandheldState  = NpadBaseState;
using NpadJoyDualState   = NpadBaseState;
using NpadJoyLeftState   = NpadBaseState;
using NpadJoyRightState  = NpadBaseState;

// ---- keyboard ------------------------------------------------------------

struct KeyboardKey {
    std::uint64_t keys[4]; // bitmask per 64-key bank
};

struct KeyboardState {
    std::uint64_t samplingNumber;
    std::uint64_t modifiers;
    KeyboardKey   keys;
};

// ---- initialisation ------------------------------------------------------

void InitializeNpad();
void SetSupportedNpadStyleSet(NpadStyleSet styleSet);
void SetSupportedNpadIdType(const NpadId* ids, std::size_t count);
void SetNpadJoyAssignmentModeSingleByDefault();

// ---- polling -------------------------------------------------------------

// Returns the number of states written (always 1 for a connected pad).
int GetNpadStates(NpadHandheldState* out, int count, NpadId id);
int GetNpadStates(NpadFullKeyState*  out, int count, NpadId id);
int GetNpadStates(NpadJoyDualState*  out, int count, NpadId id);

NpadStyleSet GetNpadStyle(NpadId id);

// ---- keyboard ------------------------------------------------------------

void InitializeKeyboard();
int GetKeyboardStates(KeyboardState* out, int count);

// ---- host hook (implement to connect real input) -------------------------
// Called once per frame before any GetNpadStates call. Fill g_hostState.
// Default: no-op (all buttons released, sticks centred).
void PollHostInput();

} // namespace nn::hid
