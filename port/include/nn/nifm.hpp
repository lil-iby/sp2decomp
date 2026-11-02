// nn::nifm — Network Interface Manager stubs.
// The game checks IsNetworkAvailable on startup; returning false is enough
// to keep it from hanging on a network request the port cannot serve.
#pragma once

#include <cstdint>

namespace nn::nifm {

enum class NetworkProfileType : std::uint32_t {
    Invalid = 0,
    WiFi    = 1,
    Ethernet= 2,
};

struct NetworkProfileData {
    std::uint8_t uuid[16];
    NetworkProfileType type;
    std::uint8_t _pad[12];
    char name[0x40];
};

// Lifecycle
int  Initialize();
void Finalize();

// State queries
bool IsNetworkAvailable();
bool IsNetworkRequestOnHold();

// Profile management (stubs — no real network profiles)
int  GetCurrentNetworkProfile(NetworkProfileData* out);
int  SetNetworkProfile(const NetworkProfileData* profile);

} // namespace nn::nifm
