// sead safe-string explicit template instantiations.
//
// SafeStringBase<char> and BufferedSafeStringBase<char> are declared with
// `extern template` in the header to prevent duplicate vtable emission across
// translation units. This TU provides the single authoritative definition.
#include "sead/safe_string.hpp"

namespace sead {

template class SafeStringBase<char>;
template class BufferedSafeStringBase<char>;

} // namespace sead
