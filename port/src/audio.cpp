// nn::audio — silent stub. Opens and updates cleanly; produces no sound.
//
// To add real audio on Windows:
//   Include <xaudio2.h>, create an IXAudio2 device in OpenAudioRenderer,
//   and submit PCM buffers in RequestUpdateAudioRenderer.
// On Linux: use PipeWire or PulseAudio in the same functions.
#include "nn/audio.hpp"

#include <cstring>

namespace nn::audio {

namespace {
constexpr std::int32_t kRevision = 6; // REV6 matches SDK 6.x game builds
} // namespace

void GetDefaultParameter(AudioRendererParameter* out) {
    if (out == nullptr) return;
    std::memset(out, 0, sizeof(*out));
    out->sampleRate            = 48000;
    out->sampleCount           = 240;     // 5ms at 48 kHz
    out->mixBufferCount        = 2;
    out->submixCount           = 1;
    out->voiceCount            = 24;
    out->sinkCount             = 1;
    out->effectCount           = 0;
    out->performanceFrameCount = 0;
    out->voiceDropEnabled      = true;
    out->splitterCount         = 0;
    out->splitterDestCount     = 0;
    out->revision              = kRevision;
}

std::size_t GetWorkBufferSize(const AudioRendererParameter*) {
    // A conservative upper bound; the real SDK computes this from the params.
    return 0x40000; // 256 KB
}

int OpenAudioRenderer(AudioRendererHandle* outHandle,
                      const AudioRendererParameter*,
                      void*, std::size_t) {
    if (outHandle == nullptr) return -1;
    std::memset(outHandle->storage, 0, sizeof(outHandle->storage));
    // Mark open with a sentinel so CloseAudioRenderer can check.
    outHandle->storage[0] = 0xAA;
    return 0;
}

void CloseAudioRenderer(AudioRendererHandle* handle) {
    if (handle == nullptr) return;
    std::memset(handle->storage, 0, sizeof(handle->storage));
}

int StartAudioRenderer(AudioRendererHandle* handle) {
    if (handle == nullptr || handle->storage[0] != 0xAA) return -1;
    return 0;
}

int StopAudioRenderer(AudioRendererHandle* handle) {
    if (handle == nullptr || handle->storage[0] != 0xAA) return -1;
    return 0;
}

int RequestUpdateAudioRenderer(AudioRendererHandle* handle,
                               void* outBuffer, std::size_t outSize,
                               const void*, std::size_t) {
    if (handle == nullptr || handle->storage[0] != 0xAA) return -1;
    if (outBuffer != nullptr && outSize > 0)
        std::memset(outBuffer, 0, outSize);
    return 0;
}

} // namespace nn::audio
