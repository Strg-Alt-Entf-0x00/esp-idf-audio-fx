#include "audio_fx/effects/delay.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace audio_fx {
namespace {

constexpr float kMaximumDelayMilliseconds = 1000.0f;
constexpr ParameterInfo kParameters[] = {
    {"mix", 0.0f, 1.0f, 0.35f},
    {"time_ms", 1.0f, kMaximumDelayMilliseconds, 250.0f},
    {"feedback", 0.0f, 0.95f, 0.35f},
};

}  // namespace

Delay::~Delay() {
    deinitialize_effect();
}

const char* Delay::name() const noexcept {
    return "delay";
}

size_t Delay::parameter_count() const noexcept {
    return sizeof(kParameters) / sizeof(kParameters[0]);
}

const ParameterInfo* Delay::parameters() const noexcept {
    return kParameters;
}

esp_err_t Delay::set_parameter(size_t index, float value) noexcept {
    if (index >= parameter_count() || !std::isfinite(value) ||
        value < kParameters[index].minimum ||
        value > kParameters[index].maximum) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (index) {
        case 0:
            return set_mix(value);
        case 1: {
            delay_time_ms_ = value;
            const size_t ramp_frames =
                (static_cast<size_t>(initialized_format().sample_rate_hz) *
                 kTimeRampMilliseconds) /
                1000;
            delay_time_ramp_remaining_ = ramp_frames > 0 ? ramp_frames : 1;
            delay_time_step_ms_ =
                (delay_time_ms_ - delay_time_current_ms_) /
                static_cast<float>(delay_time_ramp_remaining_);
            return ESP_OK;
        }
        case 2:
            feedback_ = value;
            return ESP_OK;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t Delay::get_parameter(size_t index, float* value) const noexcept {
    if (!value || index >= parameter_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (index) {
        case 0:
            *value = mix();
            break;
        case 1:
            *value = delay_time_ms_;
            break;
        case 2:
            *value = feedback_;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

size_t Delay::memory_usage_bytes() const noexcept {
    return memory_usage_bytes_;
}

esp_err_t Delay::initialize_effect() noexcept {
    const AudioFormat& format = initialized_format();
    const uint64_t required_samples =
        static_cast<uint64_t>(format.sample_rate_hz) + 2U;
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

    delay_time_ms_ = kParameters[1].default_value;
    delay_time_current_ms_ = delay_time_ms_;
    delay_time_step_ms_ = 0.0f;
    delay_time_ramp_remaining_ = 0;
    feedback_ = kParameters[2].default_value;
    initialize_mix(kParameters[0].default_value);
    return ESP_OK;
}

void Delay::deinitialize_effect() noexcept {
    release_buffers();
}

void Delay::reset_effect() noexcept {
    for (ChannelState& channel : channels_) {
        if (channel.samples) {
            std::memset(channel.samples, 0, channel.length * sizeof(float));
        }
        channel.write_position = 0;
    }
}

void Delay::process_frame(const float* input, float* wet_output) noexcept {
    const float delay_time_ms = advance_delay_time();
    const float delay_samples =
        delay_time_ms *
        static_cast<float>(initialized_format().sample_rate_hz) / 1000.0f;
    const size_t whole_delay = static_cast<size_t>(delay_samples);
    const float fraction = delay_samples - static_cast<float>(whole_delay);

    for (size_t channel_index = 0;
         channel_index < initialized_format().channels; ++channel_index) {
        ChannelState& channel = channels_[channel_index];
        channel.samples[channel.write_position] = input[channel_index];

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
}

void Delay::release_buffers() noexcept {
    for (ChannelState& channel : channels_) {
        std::free(channel.samples);
        channel = {};
    }
    memory_usage_bytes_ = 0;
}

float Delay::advance_delay_time() noexcept {
    if (delay_time_ramp_remaining_ > 0) {
        delay_time_current_ms_ += delay_time_step_ms_;
        if (--delay_time_ramp_remaining_ == 0) {
            delay_time_current_ms_ = delay_time_ms_;
        }
    }
    return delay_time_current_ms_;
}

}  // namespace audio_fx
