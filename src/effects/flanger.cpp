#include "audio_fx/effects/flanger.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace audio_fx {
namespace {

constexpr float kMaximumDelayMilliseconds = 9.0f;
constexpr float kPi = 3.14159265358979323846f;
constexpr ParameterInfo kParameters[] = {
    {"mix", 0.0f, 1.0f, 0.5f},
    {"rate_hz", 0.1f, 5.0f, 0.5f},
    {"depth_ms", 0.0f, 4.0f, 3.0f},
    {"feedback", -0.95f, 0.95f, 0.2f},
};

}  // namespace

Flanger::~Flanger() {
    deinitialize_effect();
}

const char* Flanger::name() const noexcept {
    return "flanger";
}

size_t Flanger::parameter_count() const noexcept {
    return sizeof(kParameters) / sizeof(kParameters[0]);
}

const ParameterInfo* Flanger::parameters() const noexcept {
    return kParameters;
}

esp_err_t Flanger::set_parameter(size_t index, float value) noexcept {
    if (index >= parameter_count() || !std::isfinite(value) ||
        value < kParameters[index].minimum ||
        value > kParameters[index].maximum) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (index) {
        case 0:
            return set_mix(value);
        case 1:
            rate_hz_ = value;
            return ESP_OK;
        case 2:
            depth_ms_ = value;
            return ESP_OK;
        case 3:
            feedback_ = value;
            return ESP_OK;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t Flanger::get_parameter(size_t index, float* value) const noexcept {
    if (!value || index >= parameter_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (index) {
        case 0:
            *value = mix();
            break;
        case 1:
            *value = rate_hz_;
            break;
        case 2:
            *value = depth_ms_;
            break;
        case 3:
            *value = feedback_;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

size_t Flanger::memory_usage_bytes() const noexcept {
    return memory_usage_bytes_;
}

esp_err_t Flanger::initialize_effect() noexcept {
    const AudioFormat& format = initialized_format();
    const uint64_t required_samples =
        (static_cast<uint64_t>(format.sample_rate_hz) *
         static_cast<uint64_t>(kMaximumDelayMilliseconds)) /
            1000U +
        2U;
    if (required_samples > SIZE_MAX / sizeof(float)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t length = static_cast<size_t>(required_samples);
    for (size_t channel = 0; channel < format.channels; ++channel) {
        channels_[channel].samples =
            static_cast<float*>(std::calloc(length, sizeof(float)));
        if (!channels_[channel].samples) {
            return ESP_ERR_NO_MEM;
        }
        channels_[channel].length = length;
        memory_usage_bytes_ += length * sizeof(float);
    }

    rate_hz_ = kParameters[1].default_value;
    depth_ms_ = kParameters[2].default_value;
    feedback_ = kParameters[3].default_value;
    lfo_phase_ = 0.0f;
    initialize_mix(kParameters[0].default_value);
    return ESP_OK;
}

void Flanger::deinitialize_effect() noexcept {
    release_buffers();
}

void Flanger::reset_effect() noexcept {
    for (ChannelState& channel : channels_) {
        if (channel.samples) {
            std::memset(channel.samples, 0, channel.length * sizeof(float));
        }
        channel.write_position = 0;
    }
    lfo_phase_ = 0.0f;
}

void Flanger::process_frame(const float* input, float* wet_output) noexcept {
    const size_t channel_count = initialized_format().channels;
    const float phase_increment =
        kTwoPi * rate_hz_ /
        static_cast<float>(initialized_format().sample_rate_hz);
    for (size_t channel_index = 0; channel_index < channel_count;
         ++channel_index) {
        ChannelState& channel = channels_[channel_index];
        const float phase = lfo_phase_ + (channel_index == 0 ? 0.0f : kPi);
        const float delay_ms = kBaseDelayMs + depth_ms_ * std::sin(phase);
        const float delay_samples =
            delay_ms *
            static_cast<float>(initialized_format().sample_rate_hz) / 1000.0f;
        const size_t whole_delay = static_cast<size_t>(delay_samples);
        const float fraction = delay_samples - static_cast<float>(whole_delay);
        const size_t read_position =
            (channel.write_position + channel.length - whole_delay) %
            channel.length;
        const size_t older_position =
            read_position == 0 ? channel.length - 1 : read_position - 1;
        const float delayed =
            channel.samples[read_position] * (1.0f - fraction) +
            channel.samples[older_position] * fraction;
        channel.samples[channel.write_position] =
            input[channel_index] + delayed * feedback_;
        wet_output[channel_index] = delayed;
        if (++channel.write_position == channel.length) {
            channel.write_position = 0;
        }
    }

    lfo_phase_ += phase_increment;
    if (lfo_phase_ >= kTwoPi) {
        lfo_phase_ -= kTwoPi;
    }
}

void Flanger::release_buffers() noexcept {
    for (ChannelState& channel : channels_) {
        std::free(channel.samples);
        channel = {};
    }
    memory_usage_bytes_ = 0;
}

}  // namespace audio_fx
