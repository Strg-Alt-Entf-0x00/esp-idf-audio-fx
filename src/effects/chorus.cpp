#include "audio_fx/effects/chorus.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace audio_fx {
namespace {

constexpr float kMaximumDelayMs = 30.0f;
constexpr float kPi = 3.14159265358979323846f;
constexpr ParameterInfo kParameters[] = {
    {"mix", 0.0f, 1.0f, 0.5f},
    {"rate_hz", 0.1f, 5.0f, 0.6f},
    {"depth_ms", 0.0f, 10.0f, 4.0f},
};

}  // namespace

Chorus::~Chorus() {
    deinitialize_effect();
}

const char* Chorus::name() const noexcept {
    return "chorus";
}

size_t Chorus::parameter_count() const noexcept {
    return sizeof(kParameters) / sizeof(kParameters[0]);
}

const ParameterInfo* Chorus::parameters() const noexcept {
    return kParameters;
}

esp_err_t Chorus::set_parameter(size_t index, float value) noexcept {
    if (!std::isfinite(value)) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (index) {
        case 0:
            return set_mix(value);
        case 1:
            if (value < kParameters[1].minimum || value > kParameters[1].maximum) {
                return ESP_ERR_INVALID_ARG;
            }
            rate_hz_ = value;
            return ESP_OK;
        case 2:
            if (value < kParameters[2].minimum || value > kParameters[2].maximum) {
                return ESP_ERR_INVALID_ARG;
            }
            depth_ms_ = value;
            return ESP_OK;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t Chorus::get_parameter(size_t index, float* value) const noexcept {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (index) {
        case 0:
            *value = mix();
            return ESP_OK;
        case 1:
            *value = rate_hz_;
            return ESP_OK;
        case 2:
            *value = depth_ms_;
            return ESP_OK;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

size_t Chorus::memory_usage_bytes() const noexcept {
    return memory_usage_bytes_;
}

esp_err_t Chorus::initialize_effect() noexcept {
    const AudioFormat& audio_format = initialized_format();
    const uint64_t required_samples =
        (static_cast<uint64_t>(audio_format.sample_rate_hz) *
         static_cast<uint64_t>(kMaximumDelayMs)) / 1000U + 2U;
    if (required_samples > SIZE_MAX / sizeof(float)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t buffer_length = static_cast<size_t>(required_samples);

    for (size_t channel = 0; channel < audio_format.channels; ++channel) {
        channels_[channel].samples =
            static_cast<float*>(std::calloc(buffer_length, sizeof(float)));
        if (!channels_[channel].samples) {
            return ESP_ERR_NO_MEM;
        }
        channels_[channel].length = buffer_length;
        memory_usage_bytes_ += buffer_length * sizeof(float);
    }

    rate_hz_ = kParameters[1].default_value;
    depth_ms_ = kParameters[2].default_value;
    lfo_phase_ = 0.0f;
    initialize_mix(kParameters[0].default_value);
    return ESP_OK;
}

void Chorus::deinitialize_effect() noexcept {
    release_buffers();
}

void Chorus::reset_effect() noexcept {
    for (ChannelState& channel : channels_) {
        if (channel.samples) {
            std::memset(channel.samples, 0, channel.length * sizeof(float));
        }
        channel.write_position = 0;
    }
    lfo_phase_ = 0.0f;
}

void Chorus::process_frame(const float* input, float* wet_output) noexcept {
    const size_t channel_count = initialized_format().channels;
    const float phase_increment =
        kTwoPi * rate_hz_ /
        static_cast<float>(initialized_format().sample_rate_hz);

    for (size_t channel = 0; channel < channel_count; ++channel) {
        ChannelState& state = channels_[channel];
        state.samples[state.write_position] = input[channel];

        const float phase = lfo_phase_ + (channel == 0 ? 0.0f : kPi);
        const float delay_ms = kBaseDelayMs + depth_ms_ * std::sin(phase);
        const float delay_samples =
            delay_ms * static_cast<float>(initialized_format().sample_rate_hz) /
            1000.0f;
        const size_t whole_delay = static_cast<size_t>(delay_samples);
        const float fractional_delay =
            delay_samples - static_cast<float>(whole_delay);
        const size_t read_position =
            (state.write_position + state.length - whole_delay) % state.length;
        const size_t older_position =
            read_position == 0 ? state.length - 1 : read_position - 1;
        wet_output[channel] =
            state.samples[read_position] * (1.0f - fractional_delay) +
            state.samples[older_position] * fractional_delay;

        if (++state.write_position == state.length) {
            state.write_position = 0;
        }
    }

    lfo_phase_ += phase_increment;
    if (lfo_phase_ >= kTwoPi) {
        lfo_phase_ -= kTwoPi;
    }
}

void Chorus::release_buffers() noexcept {
    for (ChannelState& channel : channels_) {
        std::free(channel.samples);
        channel = {};
    }
    memory_usage_bytes_ = 0;
}

}  // namespace audio_fx
