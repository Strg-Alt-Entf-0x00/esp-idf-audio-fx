#pragma once

#include "audio_fx/effect.hpp"

namespace audio_fx {

class Flanger final : public Effect {
public:
    Flanger() = default;
    ~Flanger() override;

    const char* name() const noexcept override;
    size_t parameter_count() const noexcept override;
    const ParameterInfo* parameters() const noexcept override;
    esp_err_t set_parameter(size_t index, float value) noexcept override;
    esp_err_t get_parameter(size_t index, float* value) const noexcept override;
    size_t memory_usage_bytes() const noexcept override;

protected:
    esp_err_t initialize_effect() noexcept override;
    void deinitialize_effect() noexcept override;
    void reset_effect() noexcept override;
    void process_frame(const float* input, float* wet_output) noexcept override;

private:
    struct ChannelState {
        float* samples = nullptr;
        size_t length = 0;
        size_t write_position = 0;
    };

    static constexpr size_t kMaxChannels = 2;
    static constexpr float kTwoPi = 6.2831853071795864769f;
    static constexpr float kBaseDelayMs = 5.0f;

    void release_buffers() noexcept;

    ChannelState channels_[kMaxChannels]{};
    float rate_hz_ = 0.5f;
    float depth_ms_ = 3.0f;
    float feedback_ = 0.2f;
    float lfo_phase_ = 0.0f;
    size_t memory_usage_bytes_ = 0;
};

}  // namespace audio_fx
