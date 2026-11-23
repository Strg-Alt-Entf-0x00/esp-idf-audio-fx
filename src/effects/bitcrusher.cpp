#include "audio_fx/effects/bitcrusher.hpp"

#include <cmath>

namespace audio_fx {
namespace {

constexpr ParameterInfo kParameters[] = {
    {"mix", 0.0f, 1.0f, 1.0f},
    {"bit_depth", 1.0f, 16.0f, 8.0f},
    {"sample_hold", 1.0f, 64.0f, 4.0f},
};

bool is_integer_parameter(float value) noexcept {
    return std::floor(value) == value;
}

}  // namespace

const char* Bitcrusher::name() const noexcept {
    return "bitcrusher";
}

size_t Bitcrusher::parameter_count() const noexcept {
    return sizeof(kParameters) / sizeof(kParameters[0]);
}

const ParameterInfo* Bitcrusher::parameters() const noexcept {
    return kParameters;
}

esp_err_t Bitcrusher::set_parameter(size_t index, float value) noexcept {
    if (index >= parameter_count() || !std::isfinite(value) ||
        value < kParameters[index].minimum ||
        value > kParameters[index].maximum) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (index) {
        case 0:
            return set_mix(value);
        case 1:
            if (!is_integer_parameter(value)) {
                return ESP_ERR_INVALID_ARG;
            }
            bit_depth_ = static_cast<unsigned int>(value);
            return ESP_OK;
        case 2:
            if (!is_integer_parameter(value)) {
                return ESP_ERR_INVALID_ARG;
            }
            sample_hold_ = static_cast<unsigned int>(value);
            hold_position_ = 0;
            return ESP_OK;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t Bitcrusher::get_parameter(size_t index, float* value) const noexcept {
    if (!value || index >= parameter_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (index) {
        case 0:
            *value = mix();
            break;
        case 1:
            *value = static_cast<float>(bit_depth_);
            break;
        case 2:
            *value = static_cast<float>(sample_hold_);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

size_t Bitcrusher::memory_usage_bytes() const noexcept {
    return sizeof(held_samples_[0]) * initialized_format().channels;
}

esp_err_t Bitcrusher::initialize_effect() noexcept {
    bit_depth_ = static_cast<unsigned int>(kParameters[1].default_value);
    sample_hold_ = static_cast<unsigned int>(kParameters[2].default_value);
    initialize_mix(kParameters[0].default_value);
    return ESP_OK;
}

void Bitcrusher::deinitialize_effect() noexcept {
    reset_effect();
}

void Bitcrusher::reset_effect() noexcept {
    for (float& sample : held_samples_) {
        sample = 0.0f;
    }
    hold_position_ = 0;
}

void Bitcrusher::process_frame(const float* input, float* wet_output) noexcept {
    const size_t channel_count = initialized_format().channels;
    if (hold_position_ == 0) {
        const unsigned int levels = (1U << bit_depth_) - 1U;
        for (size_t channel = 0; channel < channel_count; ++channel) {
            const float bounded = input[channel] < -1.0f
                                      ? -1.0f
                                      : (input[channel] > 1.0f ? 1.0f
                                                              : input[channel]);
            const float normalized = (bounded + 1.0f) * 0.5f;
            const float quantized =
                std::round(normalized * static_cast<float>(levels)) /
                static_cast<float>(levels);
            held_samples_[channel] = quantized * 2.0f - 1.0f;
        }
    }
    for (size_t channel = 0; channel < channel_count; ++channel) {
        wet_output[channel] = held_samples_[channel];
    }
    hold_position_ = (hold_position_ + 1U) % sample_hold_;
}

}  // namespace audio_fx
