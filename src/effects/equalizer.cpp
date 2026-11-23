#include "audio_fx/effects/equalizer.hpp"

#include <cmath>

namespace audio_fx {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinimumFrequencyHz = 20.0f;
constexpr float kMaximumGainDb = 12.0f;
constexpr float kMinimumQ = 0.1f;
constexpr float kMaximumQ = 10.0f;
constexpr float kMaximumNyquistFraction = 0.45f;
constexpr size_t kParameterRampMilliseconds = 5;

constexpr ParameterInfo kParameters[] = {
    {"low_freq_hz", kMinimumFrequencyHz, 20000.0f, 80.0f},
    {"low_gain_db", -kMaximumGainDb, kMaximumGainDb, 0.0f},
    {"low_q", kMinimumQ, kMaximumQ, 0.707f},
    {"low_mid_freq_hz", kMinimumFrequencyHz, 20000.0f, 250.0f},
    {"low_mid_gain_db", -kMaximumGainDb, kMaximumGainDb, 0.0f},
    {"low_mid_q", kMinimumQ, kMaximumQ, 0.707f},
    {"mid_freq_hz", kMinimumFrequencyHz, 20000.0f, 800.0f},
    {"mid_gain_db", -kMaximumGainDb, kMaximumGainDb, 0.0f},
    {"mid_q", kMinimumQ, kMaximumQ, 0.707f},
    {"high_mid_freq_hz", kMinimumFrequencyHz, 20000.0f, 1800.0f},
    {"high_mid_gain_db", -kMaximumGainDb, kMaximumGainDb, 0.0f},
    {"high_mid_q", kMinimumQ, kMaximumQ, 0.707f},
    {"high_freq_hz", kMinimumFrequencyHz, 20000.0f, 3000.0f},
    {"high_gain_db", -kMaximumGainDb, kMaximumGainDb, 0.0f},
    {"high_q", kMinimumQ, kMaximumQ, 0.707f},
};

}  // namespace

const char* Equalizer::name() const noexcept {
    return "equalizer";
}

size_t Equalizer::parameter_count() const noexcept {
    return sizeof(kParameters) / sizeof(kParameters[0]);
}

const ParameterInfo* Equalizer::parameters() const noexcept {
    return is_initialized() ? parameter_info_ : kParameters;
}

esp_err_t Equalizer::set_parameter(size_t index, float value) noexcept {
    if (index >= parameter_count() || !std::isfinite(value)) {
        return ESP_ERR_INVALID_ARG;
    }

    const ParameterInfo& info = parameters()[index];
    if (value < info.minimum || value > info.maximum) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t band_index = index / kParametersPerBand;
    const size_t parameter_in_band = index % kParametersPerBand;
    Band& band = bands_[band_index];
    Band candidate = band;
    switch (parameter_in_band) {
        case 0:
            candidate.frequency_hz = value;
            break;
        case 1:
            candidate.gain_db = value;
            break;
        case 2:
            candidate.q = value;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    Coefficients coefficients{};
    if (!calculate_coefficients(candidate, initialized_format().sample_rate_hz,
                                &coefficients)) {
        return ESP_ERR_INVALID_ARG;
    }

    candidate.target_coefficients = coefficients;
    const size_t ramp_frames =
        (static_cast<size_t>(initialized_format().sample_rate_hz) *
         kParameterRampMilliseconds) /
        1000;
    update_coefficient_ramp(&candidate, ramp_frames > 0 ? ramp_frames : 1);
    band = candidate;
    return ESP_OK;
}

esp_err_t Equalizer::get_parameter(size_t index, float* value) const noexcept {
    if (!value || index >= parameter_count()) {
        return ESP_ERR_INVALID_ARG;
    }

    const Band& band = bands_[index / kParametersPerBand];
    switch (index % kParametersPerBand) {
        case 0:
            *value = band.frequency_hz;
            return ESP_OK;
        case 1:
            *value = band.gain_db;
            return ESP_OK;
        case 2:
            *value = band.q;
            return ESP_OK;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

size_t Equalizer::memory_usage_bytes() const noexcept {
    if (!is_initialized()) {
        return 0;
    }

    return sizeof(bands_) + sizeof(parameter_info_) +
           sizeof(states_[0][0]) * initialized_format().channels * kBandCount;
}

esp_err_t Equalizer::initialize_effect() noexcept {
    for (size_t index = 0; index < parameter_count(); ++index) {
        parameter_info_[index] = kParameters[index];
        if (index % kParametersPerBand == 0) {
            const float maximum_frequency_hz =
                static_cast<float>(initialized_format().sample_rate_hz) *
                kMaximumNyquistFraction;
            if (maximum_frequency_hz < parameter_info_[index].maximum) {
                parameter_info_[index].maximum = maximum_frequency_hz;
            }
        }
    }

    for (size_t index = 0; index < kBandCount; ++index) {
        Band& band = bands_[index];
        band.frequency_hz =
            kParameters[index * kParametersPerBand].default_value;
        band.gain_db = kParameters[index * kParametersPerBand + 1].default_value;
        band.q = kParameters[index * kParametersPerBand + 2].default_value;
        if (!calculate_coefficients(band,
                                    initialized_format().sample_rate_hz,
                                    &band.coefficients)) {
            return ESP_ERR_INVALID_ARG;
        }
        band.target_coefficients = band.coefficients;
        band.ramp_remaining = 0;
        band.coefficient_step = {};
    }
    initialize_mix(1.0f);
    return ESP_OK;
}

void Equalizer::deinitialize_effect() noexcept {
    reset_effect();
}

void Equalizer::reset_effect() noexcept {
    for (auto& channel : states_) {
        for (FilterState& state : channel) {
            state = {};
        }
    }
}

void Equalizer::process_frame(const float* input, float* wet_output) noexcept {
    const size_t channel_count = initialized_format().channels;
    for (Band& band : bands_) {
        advance_coefficient_ramp(&band);
    }

    for (size_t channel = 0; channel < channel_count; ++channel) {
        float sample = input[channel];
        for (size_t band_index = 0; band_index < kBandCount; ++band_index) {
            const Coefficients& coefficients = bands_[band_index].coefficients;
            FilterState& state = states_[channel][band_index];
            const float output = coefficients.b0 * sample + state.z1;
            state.z1 = coefficients.b1 * sample - coefficients.a1 * output +
                       state.z2;
            state.z2 = coefficients.b2 * sample - coefficients.a2 * output;
            sample = output;
        }
        wet_output[channel] = sample;
    }
}

bool Equalizer::calculate_coefficients(
    const Band& band, uint32_t sample_rate_hz,
    Coefficients* coefficients) noexcept {
    if (!coefficients || !std::isfinite(band.frequency_hz) ||
        !std::isfinite(band.gain_db) || !std::isfinite(band.q) ||
        band.frequency_hz < kMinimumFrequencyHz ||
        band.frequency_hz >
            static_cast<float>(sample_rate_hz) * kMaximumNyquistFraction ||
        band.q < kMinimumQ || band.q > kMaximumQ ||
        band.gain_db < -kMaximumGainDb || band.gain_db > kMaximumGainDb) {
        return false;
    }

    const float omega =
        2.0f * kPi * band.frequency_hz / static_cast<float>(sample_rate_hz);
    const float sine = std::sin(omega);
    const float cosine = std::cos(omega);
    const float amplitude = std::pow(10.0f, band.gain_db / 40.0f);
    const float alpha = sine / (2.0f * band.q);
    const float a0 = 1.0f + alpha / amplitude;
    if (!std::isfinite(a0) || a0 <= 0.0f) {
        return false;
    }

    coefficients->b0 = (1.0f + alpha * amplitude) / a0;
    coefficients->b1 = (-2.0f * cosine) / a0;
    coefficients->b2 = (1.0f - alpha * amplitude) / a0;
    coefficients->a1 = (-2.0f * cosine) / a0;
    coefficients->a2 = (1.0f - alpha / amplitude) / a0;
    return std::isfinite(coefficients->b0) &&
           std::isfinite(coefficients->b1) &&
           std::isfinite(coefficients->b2) &&
           std::isfinite(coefficients->a1) &&
           std::isfinite(coefficients->a2);
}

void Equalizer::update_coefficient_ramp(Band* band,
                                        size_t ramp_frames) noexcept {
    const Coefficients& current = band->coefficients;
    const Coefficients& target = band->target_coefficients;
    const float divisor = static_cast<float>(ramp_frames);
    band->coefficient_step = {
        (target.b0 - current.b0) / divisor,
        (target.b1 - current.b1) / divisor,
        (target.b2 - current.b2) / divisor,
        (target.a1 - current.a1) / divisor,
        (target.a2 - current.a2) / divisor,
    };
    band->ramp_remaining = ramp_frames;
}

void Equalizer::advance_coefficient_ramp(Band* band) noexcept {
    if (band->ramp_remaining == 0) {
        return;
    }

    Coefficients& current = band->coefficients;
    const Coefficients& target = band->target_coefficients;
    const Coefficients& step = band->coefficient_step;
    current.b0 += step.b0;
    current.b1 += step.b1;
    current.b2 += step.b2;
    current.a1 += step.a1;
    current.a2 += step.a2;
    if (--band->ramp_remaining == 0) {
        current = target;
    }
}

}  // namespace audio_fx
