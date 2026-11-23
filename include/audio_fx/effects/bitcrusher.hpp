#pragma once

#include "audio_fx/effect.hpp"

namespace audio_fx {

class Bitcrusher final : public Effect {
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
    static constexpr size_t kMaxChannels = 2;

    unsigned int bit_depth_ = 8;
    unsigned int sample_hold_ = 4;
    unsigned int hold_position_ = 0;
    float held_samples_[kMaxChannels]{};
};

}  // namespace audio_fx
