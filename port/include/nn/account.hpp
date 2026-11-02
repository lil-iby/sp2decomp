// nn::account — user account stubs.
// Returns a single synthetic guest user so the game's account check succeeds.
#pragma once

#include <cstdint>
#include <cstddef>

namespace nn::account {

// 128-bit user identifier.
struct Uid {
    std::uint64_t high;
    std::uint64_t low;

    bool operator==(const Uid& o) const { return high == o.high && low == o.low; }
    bool operator!=(const Uid& o) const { return !(*this == o); }
};

// The one synthetic user the stub exposes.
constexpr Uid kGuestUid = { 0xDEADBEEFCAFEBABEULL, 0x0102030405060708ULL };

struct UserHandle {
    Uid uid;
};

// Lifecycle
int  Initialize();
void Finalize();

// User enumeration
int  GetUserCount(int* outCount);
int  ListAllUsers(Uid* out, int* outCount, int maxCount);

// Handle management
int  OpenUser(UserHandle* outHandle, Uid uid);
void CloseUser(UserHandle* handle);

// Profile
int  GetNickname(char* outNickname, std::size_t size, UserHandle handle);
bool IsUserRegistrationRequestAvailable();

} // namespace nn::account
