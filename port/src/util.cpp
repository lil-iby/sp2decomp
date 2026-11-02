// nn::util / nn::diag implementation — host stand-ins.
#include "nn/util.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace nn::util {

int VSNPrintf(char *dst, std::size_t size, const char *format, std::va_list *args) {
    if (dst == nullptr || format == nullptr) return -1;
    if (size == 0) return 0;
    // The observed call sites pass a POINTER to a va_list, which is the SDK's own ABI
    // quirk; host vsnprintf takes the list by value.
    if (args == nullptr) {
        dst[0] = '\0';
        return 0;
    }
    return std::vsnprintf(dst, size, format, *args);
}

int SNPrintf(char *dst, std::size_t size, const char *format, ...) {
    if (dst == nullptr || format == nullptr) return -1;
    if (size == 0) return 0;
    std::va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(dst, size, format, args);
    va_end(args);
    return n;
}

namespace {
constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::size_t ToBase64String(char *dst, std::size_t dstSize, const void *src,
                           std::size_t srcSize, Base64Mode /*mode*/) {
    if (dst == nullptr || src == nullptr) return 0;
    const auto *in = static_cast<const unsigned char *>(src);
    const std::size_t need = ((srcSize + 2) / 3) * 4;
    if (dstSize < need + 1) return 0;
    std::size_t o = 0;
    for (std::size_t i = 0; i < srcSize; i += 3) {
        const unsigned b0 = in[i];
        const unsigned b1 = i + 1 < srcSize ? in[i + 1] : 0;
        const unsigned b2 = i + 2 < srcSize ? in[i + 2] : 0;
        const unsigned t = (b0 << 16) | (b1 << 8) | b2;
        dst[o++] = kB64[(t >> 18) & 0x3f];
        dst[o++] = kB64[(t >> 12) & 0x3f];
        dst[o++] = i + 1 < srcSize ? kB64[(t >> 6) & 0x3f] : '=';
        dst[o++] = i + 2 < srcSize ? kB64[t & 0x3f] : '=';
    }
    dst[o] = '\0';
    return o;
}

std::size_t FromBase64String(void *dst, std::size_t dstSize, const char *src,
                             std::size_t srcSize, Base64Mode /*mode*/) {
    if (dst == nullptr || src == nullptr || srcSize < 4) return 0;
    auto *out = static_cast<unsigned char *>(dst);
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::size_t o = 0;
    for (std::size_t i = 0; i + 3 < srcSize && o + 2 < dstSize; i += 4) {
        const int a = val(src[i]), b = val(src[i + 1]);
        const int c = src[i + 2] == '=' ? -2 : val(src[i + 2]);
        const int d = src[i + 3] == '=' ? -2 : val(src[i + 3]);
        if (a < 0 || b < 0) break;
        if (o < dstSize) out[o++] = static_cast<unsigned char>((a << 2) | (b >> 4));
        if (c >= 0 && o < dstSize) out[o++] = static_cast<unsigned char>(((b & 0xf) << 4) | (c >> 2));
        if (d >= 0 && o < dstSize) out[o++] = static_cast<unsigned char>(((c & 0x3) << 6) | d);
    }
    return o;
}

} // namespace nn::util

namespace nn::diag {

namespace detail {

void LogImpl(LogMetaData *meta, const char *format, ...) {
    (void)meta;
    if (format == nullptr) return;
    std::va_list args;
    va_start(args, format);
    // The observed formats are real printf strings used for boot diagnostics such as
    // "%4ld MB: Program\n", so emit them to stdout rather than swallowing them.
    std::vfprintf(stdout, format, args);
    va_end(args);
    std::fflush(stdout);
}

} // namespace detail

void AbortImpl(const char *message, const char *condition, const char *file, int line) {
    std::fprintf(stderr, "\n[abort] %s:%d\n  condition: %s\n  message: %s\n",
                 file != nullptr ? file : "<unknown>", line,
                 condition != nullptr ? condition : "<none>",
                 message != nullptr ? message : "<none>");
    std::fflush(stderr);
    std::abort();
}

} // namespace nn::diag
