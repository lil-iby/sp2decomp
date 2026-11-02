// nn::audio — audio renderer stubs.
//
// The boot path calls OpenAudioRenderer once and RequestUpdateAudioRenderer in
// the main loop. The stubs succeed silently so boot does not abort on audio.
// Real audio: implement the body of Update() in port/src/audio.cpp using
// XAudio2 (Windows) or PipeWire/ALSA (Linux).
#pragma once

#include <cstdint>
#include <cstddef>

namespace nn::audio {

// ---- parameter / config --------------------------------------------------

struct AudioRendererParameter {
    std::int32_t  sampleRate;          // 32000 or 48000
    std::int32_t  sampleCount;         // samples per update
    std::int32_t  mixBufferCount;
    std::int32_t  submixCount;
    std::int32_t  voiceCount;
    std::int32_t  sinkCount;
    std::int32_t  effectCount;
    std::int32_t  performanceFrameCount;
    bool          voiceDropEnabled;
    std::uint8_t  _pad0;
    std::uint16_t _pad1;
    std::int32_t  splitterCount;
    std::int32_t  splitterDestCount;
    std::int32_t  _unk;
    std::int32_t  revision;
};

void GetDefaultParameter(AudioRendererParameter* out);
std::size_t GetWorkBufferSize(const AudioRendererParameter* param);

// ---- renderer lifecycle --------------------------------------------------

// Opaque handle; the implementation stores state behind a pointer in storage.
struct AudioRendererHandle {
    alignas(8) std::uint8_t storage[64];
};

int  OpenAudioRenderer(AudioRendererHandle* outHandle,
                       const AudioRendererParameter* param,
                       void* workBuffer, std::size_t workBufferSize);
void CloseAudioRenderer(AudioRendererHandle* handle);
int  StartAudioRenderer(AudioRendererHandle* handle);
int  StopAudioRenderer(AudioRendererHandle* handle);
int  RequestUpdateAudioRenderer(AudioRendererHandle* handle,
                                void* outBuffer, std::size_t outSize,
                                const void* inBuffer, std::size_t inSize);

} // namespace nn::audio
