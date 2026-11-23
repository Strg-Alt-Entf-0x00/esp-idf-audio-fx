#pragma once

#include "audio_fx/effect.hpp"

namespace audio_fx {

class Equalizer final : public Effect {
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
    static constexpr size_t kBandCount = 5;
    static constexpr size_t kMaxChannels = 2;
    static constexpr size_t kParametersPerBand = 3;

    struct Coefficients {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
    };

    struct FilterState {
        float z1 = 0.0f;
        float z2 = 0.0f;
    };

    struct Band {
        float frequency_hz;
        float gain_db;
        float q;
        Coefficients coefficients;
        Coefficients target_coefficients;
        Coefficients coefficient_step;
        size_t ramp_remaining = 0;
    };

    static bool calculate_coefficients(const Band& band, uint32_t sample_rate_hz,
                                       Coefficients* coefficients) noexcept;
    static void update_coefficient_ramp(Band* band, size_t ramp_frames) noexcept;
    static void advance_coefficient_ramp(Band* band) noexcept;

    Band bands_[kBandCount]{};
    ParameterInfo parameter_info_[kBandCount * kParametersPerBand]{};
    FilterState states_[kMaxChannels][kBandCount]{};
};

}  // namespace audio_fx
