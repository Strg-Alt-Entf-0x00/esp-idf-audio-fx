#pragma once

#include "audio_fx/effect.hpp"

namespace audio_fx {

class Delay final : public Effect {
public:
    Delay() = default;
    ~Delay() override;

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
    static constexpr size_t kTimeRampMilliseconds = 20;

    void release_buffers() noexcept;
    float advance_delay_time() noexcept;

    ChannelState channels_[kMaxChannels]{};
    float delay_time_ms_ = 250.0f;
    float delay_time_current_ms_ = 250.0f;
    float delay_time_step_ms_ = 0.0f;
    size_t delay_time_ramp_remaining_ = 0;
    float feedback_ = 0.35f;
    size_t memory_usage_bytes_ = 0;
};

}  // namespace audio_fx
