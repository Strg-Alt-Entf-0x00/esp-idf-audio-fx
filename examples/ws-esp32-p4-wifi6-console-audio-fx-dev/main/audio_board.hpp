#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace waveshare_audio {

enum class MicrophoneInput : uint8_t {
    codec,
    inmp441,
};

struct OutputDiagnostics {
    uint32_t dma_completions = 0;
    uint32_t dma_bytes = 0;
    uint32_t dma_non_silent_buffers = 0;
    uint32_t dma_silent_buffers = 0;
    uint32_t dma_peak_sample = 0;
    uint32_t dma_buffer_bytes = 0;
};

esp_err_t initialize() noexcept;
esp_err_t start() noexcept;
esp_err_t start_output() noexcept;
esp_err_t stop() noexcept;
esp_err_t stop_output() noexcept;
esp_err_t set_microphone_input(MicrophoneInput input) noexcept;
esp_err_t read(int16_t* interleaved_stereo, size_t buffer_bytes,
               size_t* bytes_read, uint32_t timeout_ms) noexcept;
esp_err_t write(const int16_t* interleaved_stereo, size_t buffer_bytes,
                size_t* bytes_written, uint32_t timeout_ms) noexcept;
esp_err_t set_speaker_volume(float volume_db) noexcept;
esp_err_t set_microphone_gain(float gain_db) noexcept;
void reset_output_diagnostics() noexcept;
void get_output_diagnostics(OutputDiagnostics* diagnostics) noexcept;

}  // namespace waveshare_audio
