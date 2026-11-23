#pragma once

#include "audio_fx/effect.hpp"

namespace audio_fx {

class Compressor final : public Effect {
public:
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
    void update_time_coefficients() noexcept;

    float threshold_db_ = -18.0f;
    float ratio_ = 4.0f;
    float attack_ms_ = 10.0f;
    float release_ms_ = 100.0f;
    float knee_db_ = 6.0f;
    float makeup_db_ = 0.0f;
    float envelope_ = 0.0f;
    float attack_coefficient_ = 0.0f;
    float release_coefficient_ = 0.0f;
};

}  // namespace audio_fx
