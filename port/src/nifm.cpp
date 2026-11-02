// nn::nifm — network stubs. Reports no network so boot doesn't hang.
#include "nn/nifm.hpp"

#include <cstring>

namespace nn::nifm {

int  Initialize()               { return 0; }
void Finalize()                 {}
bool IsNetworkAvailable()       { return false; }
bool IsNetworkRequestOnHold()   { return false; }

int GetCurrentNetworkProfile(NetworkProfileData* out) {
    if (out == nullptr) return -1;
    std::memset(out, 0, sizeof(*out));
    return -1; // no profile available
}

int SetNetworkProfile(const NetworkProfileData*) { return -1; }

} // namespace nn::nifm
