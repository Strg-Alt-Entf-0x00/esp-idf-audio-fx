#pragma once

#include "audio_fx/effect.hpp"

namespace audio_fx {

class Reverb final : public Effect {
public:
    Reverb() = default;
    ~Reverb() override;

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
    struct DelayLine {
        float* samples = nullptr;
        size_t length = 0;
        size_t position = 0;
    };

    struct CombLine : DelayLine {
        float filter_store = 0.0f;
    };

    static constexpr size_t kCombCount = 8;
    static constexpr size_t kAllpassCount = 4;
    static constexpr size_t kMaxChannels = 2;

    void release_lines() noexcept;

    CombLine combs_[kMaxChannels][kCombCount]{};
    DelayLine allpasses_[kMaxChannels][kAllpassCount]{};
    float room_size_ = 0.7f;
    float damping_ = 0.2f;
    size_t memory_usage_bytes_ = 0;
};

}  // namespace audio_fx
