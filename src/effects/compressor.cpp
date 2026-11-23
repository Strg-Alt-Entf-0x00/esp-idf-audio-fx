#include "audio_fx/effects/compressor.hpp"

#include <algorithm>
#include <cmath>

namespace audio_fx {
namespace {

constexpr float kMinimumEnvelope = 1.0e-8f;
constexpr float kPi = 3.14159265358979323846f;
constexpr ParameterInfo kParameters[] = {
    {"threshold_db", -60.0f, 0.0f, -18.0f},
    {"ratio", 1.0f, 20.0f, 4.0f},
    {"attack_ms", 0.1f, 100.0f, 10.0f},
    {"release_ms", 10.0f, 1000.0f, 100.0f},
    {"knee_db", 0.0f, 24.0f, 6.0f},
    {"makeup_db", -12.0f, 24.0f, 0.0f},
    {"mix", 0.0f, 1.0f, 1.0f},
};

float smoothing_coefficient(float milliseconds,
                            uint32_t sample_rate_hz) noexcept {
    const float time_seconds = milliseconds * 0.001f;
    return std::exp(-1.0f /
                    (time_seconds * static_cast<float>(sample_rate_hz)));
}

}  // namespace

const char* Compressor::name() const noexcept {
    return "compressor";
}

size_t Compressor::parameter_count() const noexcept {
    return sizeof(kParameters) / sizeof(kParameters[0]);
}

const ParameterInfo* Compressor::parameters() const noexcept {
    return kParameters;
}

esp_err_t Compressor::set_parameter(size_t index, float value) noexcept {
    if (index >= parameter_count() || !std::isfinite(value) ||
        value < kParameters[index].minimum ||
        value > kParameters[index].maximum) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (index) {
        case 0:
            threshold_db_ = value;
            break;
        case 1:
            ratio_ = value;
            break;
        case 2:
            attack_ms_ = value;
            update_time_coefficients();
            break;
        case 3:
            release_ms_ = value;
            update_time_coefficients();
            break;
        case 4:
            knee_db_ = value;
            break;
        case 5:
            makeup_db_ = value;
            break;
        case 6:
            return set_mix(value);
        default:
            return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t Compressor::get_parameter(size_t index, float* value) const noexcept {
    if (!value || index >= parameter_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (index) {
        case 0:
            *value = threshold_db_;
            break;
        case 1:
            *value = ratio_;
            break;
        case 2:
            *value = attack_ms_;
            break;
        case 3:
            *value = release_ms_;
            break;
        case 4:
            *value = knee_db_;
            break;
        case 5:
            *value = makeup_db_;
            break;
        case 6:
            *value = mix();
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

size_t Compressor::memory_usage_bytes() const noexcept {
    if (!is_initialized()) {
        return 0;
    }
    return sizeof(envelope_) + sizeof(attack_coefficient_) +
           sizeof(release_coefficient_);
}

esp_err_t Compressor::initialize_effect() noexcept {
    threshold_db_ = kParameters[0].default_value;
    ratio_ = kParameters[1].default_value;
    attack_ms_ = kParameters[2].default_value;
    release_ms_ = kParameters[3].default_value;
    knee_db_ = kParameters[4].default_value;
    makeup_db_ = kParameters[5].default_value;
    update_time_coefficients();
    initialize_mix(kParameters[6].default_value);
    return ESP_OK;
}

void Compressor::deinitialize_effect() noexcept {
    reset_effect();
}

void Compressor::reset_effect() noexcept {
    envelope_ = 0.0f;
}

void Compressor::process_frame(const float* input, float* wet_output) noexcept {
    float peak = 0.0f;
    const size_t channel_count = initialized_format().channels;
    for (size_t channel = 0; channel < channel_count; ++channel) {
        peak = std::max(peak, std::fabs(input[channel]));
    }

    const float coefficient =
        peak > envelope_ ? attack_coefficient_ : release_coefficient_;
    envelope_ = coefficient * envelope_ + (1.0f - coefficient) * peak;
    const float level_db =
        20.0f * std::log10(std::max(envelope_, kMinimumEnvelope));
    const float half_knee = knee_db_ * 0.5f;
    float reduction_db = 0.0f;
    if (knee_db_ > 0.0f &&
        level_db > threshold_db_ - half_knee &&
        level_db < threshold_db_ + half_knee) {
        const float distance = level_db - threshold_db_ + half_knee;
        reduction_db =
            -(1.0f - 1.0f / ratio_) * distance * distance / (2.0f * knee_db_);
    } else if (level_db >= threshold_db_ + half_knee) {
        reduction_db =
            -(level_db - threshold_db_) * (1.0f - 1.0f / ratio_);
    }
    const float gain =
        std::pow(10.0f, (reduction_db + makeup_db_) / 20.0f);
    for (size_t channel = 0; channel < channel_count; ++channel) {
        wet_output[channel] = input[channel] * gain;
    }
}

void Compressor::update_time_coefficients() noexcept {
    const uint32_t sample_rate_hz =
        initialized_format().sample_rate_hz;
    if (sample_rate_hz == 0) {
        return;
    }
    attack_coefficient_ = smoothing_coefficient(attack_ms_, sample_rate_hz);
    release_coefficient_ = smoothing_coefficient(release_ms_, sample_rate_hz);
}

}  // namespace audio_fx
