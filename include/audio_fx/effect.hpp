#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace audio_fx {

struct AudioFormat {
    uint32_t sample_rate_hz = 0;
    uint8_t channels = 0;
};

struct ParameterInfo {
    const char* name;
    float minimum;
    float maximum;
    float default_value;
};

class Effect {
public:
    virtual ~Effect() = default;

    Effect(const Effect&) = delete;
    Effect& operator=(const Effect&) = delete;
    Effect(Effect&&) = delete;
    Effect& operator=(Effect&&) = delete;

    esp_err_t initialize(const AudioFormat& format) noexcept;
    esp_err_t process(const float* input, float* output,
                      size_t frame_count) noexcept;
    void reset() noexcept;

    void set_enabled(bool enabled) noexcept;
    bool is_enabled() const noexcept;

    bool is_initialized() const noexcept;
    const AudioFormat& format() const noexcept;

    virtual const char* name() const noexcept = 0;
    virtual size_t parameter_count() const noexcept = 0;
    virtual const ParameterInfo* parameters() const noexcept = 0;
    virtual esp_err_t set_parameter(size_t index, float value) noexcept = 0;
    virtual esp_err_t get_parameter(size_t index, float* value) const noexcept = 0;
    virtual size_t memory_usage_bytes() const noexcept = 0;

protected:
    Effect() = default;

    esp_err_t set_mix(float value) noexcept;
    void initialize_mix(float value) noexcept;

    virtual esp_err_t initialize_effect() noexcept = 0;
    virtual void deinitialize_effect() noexcept = 0;
    virtual void reset_effect() noexcept = 0;
    virtual void process_frame(const float* input, float* wet_output) noexcept = 0;

    float mix() const noexcept;
    const AudioFormat& initialized_format() const noexcept;

private:
    void update_mix_target() noexcept;
    float advance_mix() noexcept;

    AudioFormat format_{};
    float mix_value_ = 0.0f;
    float mix_current_ = 0.0f;
    float mix_target_ = 0.0f;
    float mix_step_ = 0.0f;
    size_t mix_ramp_remaining_ = 0;
    bool enabled_ = true;
    bool initialized_ = false;
};

}  // namespace audio_fx
