// nn::account — single synthetic guest user.
#include "nn/account.hpp"

#include <cstring>

namespace nn::account {

namespace {
bool g_initialized = false;
} // namespace

int Initialize() {
    g_initialized = true;
    return 0;
}

void Finalize() { g_initialized = false; }

int GetUserCount(int* outCount) {
    if (outCount == nullptr) return -1;
    *outCount = 1;
    return 0;
}

int ListAllUsers(Uid* out, int* outCount, int maxCount) {
    if (out == nullptr || outCount == nullptr || maxCount <= 0) return -1;
    out[0] = kGuestUid;
    *outCount = 1;
    return 0;
}

int OpenUser(UserHandle* outHandle, Uid uid) {
    if (outHandle == nullptr) return -1;
    outHandle->uid = uid;
    return 0;
}

void CloseUser(UserHandle*) {}

int GetNickname(char* outNickname, std::size_t size, UserHandle) {
    if (outNickname == nullptr || size == 0) return -1;
    const char* name = "Player";
    std::size_t len = 6;
    if (len + 1 > size) return -1;
    std::memcpy(outNickname, name, len + 1);
    return 0;
}

bool IsUserRegistrationRequestAvailable() { return false; }

} // namespace nn::account
