#pragma once
// sead string classes — ported from BOOT_CALLGRAPH.md / ENGINE_REFERENCE.md.
//
// Three classes, in order of derivation:
//
//   SafeStringBase<T>          — read-only view over a const T* (no allocation)
//   BufferedSafeStringBase<T>  — adds a mutable backing buffer + capacity
//   FormatFixedSafeString<N>   — stack-allocated formatted string (N chars incl. NUL)
//
// Evidence:
//   BufferedSafeStringBase<char> ctor       0x7100e851d0  300 bytes  depth 1
//   BufferedSafeStringBase::assureTermImpl_ 0x7100e84ccc   20 bytes  depth 1
//   FormatFixedSafeString (ctor)            0x710000dbec  224 bytes  depth 1
//
// The real binary has a vtable per class (indirect calls observed at 4 sites for
// BufferedSafeStringBase). The port mirrors the virtual override exactly so that
// any future vtable reconstruction matches.
//
// nn::util::VSNPrintf is used for formatting (the SDK's own vsnprintf wrapper that
// takes a va_list* rather than va_list, as observed at all 17 call sites).
#include "nn/util.hpp"
#include <cstdarg>
#include <cstddef>

// MSVC does not implement __attribute__((format)) — guard it.
#ifdef __GNUC__
#  define SEAD_PRINTF_FORMAT(fmt_idx, first_arg) __attribute__((format(printf, fmt_idx, first_arg)))
#else
#  define SEAD_PRINTF_FORMAT(fmt_idx, first_arg)
#endif

namespace sead {

// ---- SafeStringBase<T> -----------------------------------------------------
// Read-only string view. The binary stores one pointer (mStringTop) which points
// either into a literal or into a BufferedSafeStringBase's mBuffer.
template<typename T>
class SafeStringBase {
public:
    constexpr SafeStringBase() : mStringTop(sEmpty) {}
    constexpr explicit SafeStringBase(const T *str) : mStringTop(str ? str : sEmpty) {}

    virtual ~SafeStringBase() = default;

    const T *cstr() const { return mStringTop; }
    bool isEmpty() const { return mStringTop == nullptr || mStringTop[0] == static_cast<T>(0); }

    // Virtual: overridden by BufferedSafeStringBase to null-terminate the buffer.
    virtual void assureTerminationImpl_() const {}

protected:
    const T *mStringTop;

private:
    static constexpr T sEmpty[1] = { static_cast<T>(0) };
};

// Explicit instantiation declaration so the compiler emits the template body once.
extern template class SafeStringBase<char>;

// ---- BufferedSafeStringBase<T> ---------------------------------------------
// Adds a writable buffer pointer and a capacity. The virtual assureTerminationImpl_
// writes a NUL at mBuffer[mBufferSize-1] (observed body: 20 bytes, 8 body lines).
template<typename T>
class BufferedSafeStringBase : public SafeStringBase<T> {
public:
    // 0x7100e851d0. Stores buffer and capacity; NUL-terminates on construction.
    BufferedSafeStringBase(T *buf, int capacity)
        : SafeStringBase<T>(buf), mBuffer(buf), mBufferSize(capacity) {
        if (buf != nullptr && capacity > 0)
            buf[0] = static_cast<T>(0);
    }

    // 0x7100e84ccc. Ensures the string is properly NUL-terminated.
    void assureTerminationImpl_() const override {
        if (mBuffer != nullptr && mBufferSize > 0)
            mBuffer[mBufferSize - 1] = static_cast<T>(0);
    }

    T *getBuffer() const { return mBuffer; }
    int getBufferSize() const { return mBufferSize; }

    // In-place vsnprintf into the backing buffer.
    int format(const T *fmt, ...) {
        if (mBuffer == nullptr || mBufferSize <= 0) return -1;
        std::va_list args;
        va_start(args, fmt);
        const int n = ::nn::util::VSNPrintf(mBuffer, static_cast<std::size_t>(mBufferSize),
                                            fmt, &args);
        va_end(args);
        assureTerminationImpl_();
        this->mStringTop = mBuffer;
        return n;
    }

protected:
    T *mBuffer;
    int mBufferSize;
};

extern template class BufferedSafeStringBase<char>;

// ---- FormatFixedSafeString<N> ----------------------------------------------
// Stack-allocated formatted string. N includes the NUL terminator.
// 0x710000dbec is the observed ctor entry; it calls BufferedSafeStringBase<char>
// then formats via nn::util::VSNPrintf, then assureTerminationImpl_.
template<int N>
class FormatFixedSafeString : public BufferedSafeStringBase<char> {
    static_assert(N > 0, "FormatFixedSafeString capacity must be positive");
public:
    explicit FormatFixedSafeString(const char *fmt, ...) SEAD_PRINTF_FORMAT(2, 3)
        : BufferedSafeStringBase<char>(mBuf, N)
    {
        if (fmt != nullptr) {
            std::va_list args;
            va_start(args, fmt);
            ::nn::util::VSNPrintf(mBuf, static_cast<std::size_t>(N), fmt, &args);
            va_end(args);
        }
        assureTerminationImpl_();
        mStringTop = mBuf;
    }

private:
    char mBuf[N];
};

} // namespace sead
