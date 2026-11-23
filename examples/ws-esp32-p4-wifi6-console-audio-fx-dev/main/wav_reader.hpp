#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "esp_err.h"

namespace audio_fx_demo {

class WavReader {
public:
    WavReader() = default;
    ~WavReader();

    WavReader(const WavReader&) = delete;
    WavReader& operator=(const WavReader&) = delete;

    esp_err_t open(const char* path) noexcept;
    esp_err_t read_stereo(float* interleaved_output, size_t max_frames,
                          size_t* frames_read) noexcept;
    void close() noexcept;
    bool is_open() const noexcept;

private:
    FILE* file_ = nullptr;
    uint16_t channels_ = 0;
    uint16_t block_align_ = 0;
    uint32_t data_remaining_ = 0;
};

}  // namespace audio_fx_demo
