#include "audio_fx/effects/limiter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace audio_fx {
namespace {

constexpr ParameterInfo kParameters[] = {
    {"ceiling_db", -12.0f, 0.0f, -1.0f},
    {"release_ms", 10.0f, 1000.0f, 100.0f},
};

}  // namespace

Limiter::~Limiter() {
    deinitialize_effect();
}

const char* Limiter::name() const noexcept {
    return "limiter";
}

size_t Limiter::parameter_count() const noexcept {
    return sizeof(kParameters) / sizeof(kParameters[0]);
}

const ParameterInfo* Limiter::parameters() const noexcept {
    return kParameters;
}

esp_err_t Limiter::set_parameter(size_t index, float value) noexcept {
    if (index >= parameter_count() || !std::isfinite(value) ||
        value < kParameters[index].minimum ||
        value > kParameters[index].maximum) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (index) {
        case 0:
            ceiling_db_ = value;
            ceiling_linear_ = std::pow(10.0f, ceiling_db_ / 20.0f);
            return ESP_OK;
        case 1:
            release_ms_ = value;
            update_release_coefficient();
            return ESP_OK;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t Limiter::get_parameter(size_t index, float* value) const noexcept {
    if (!value || index >= parameter_count()) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (index) {
        case 0:
            *value = ceiling_db_;
            break;
        case 1:
            *value = release_ms_;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

size_t Limiter::memory_usage_bytes() const noexcept {
    if (!is_initialized()) {
        return 0;
    }
    return memory_usage_bytes_ + sizeof(gain_) + sizeof(frame_index_) +
           sizeof(peak_queue_head_) + sizeof(peak_queue_tail_) +
           sizeof(peak_queue_count_) + sizeof(release_coefficient_);
}

esp_err_t Limiter::initialize_effect() noexcept {
    const AudioFormat& format = initialized_format();
    const size_t lookahead_frames =
        (static_cast<size_t>(format.sample_rate_hz) *
         kLookaheadMilliseconds) /
        1000;
    if (lookahead_frames == 0 || lookahead_frames >= SIZE_MAX ||
        lookahead_frames + 1 > SIZE_MAX / sizeof(float) ||
        lookahead_frames + 1 > SIZE_MAX / sizeof(size_t)) {
        return ESP_ERR_INVALID_SIZE;
    }
    lookahead_frames_ = lookahead_frames;
    for (size_t channel = 0; channel < format.channels; ++channel) {
        ChannelState& state = channels_[channel];
        state.samples =
            static_cast<float*>(std::calloc(lookahead_frames_, sizeof(float)));
        if (!state.samples) {
            return ESP_ERR_NO_MEM;
        }
        state.length = lookahead_frames_;
        memory_usage_bytes_ += lookahead_frames_ * sizeof(float);
    }

    peak_queue_capacity_ = lookahead_frames_ + 1;
    peak_values_ = static_cast<float*>(
        std::calloc(peak_queue_capacity_, sizeof(float)));
    peak_indices_ = static_cast<size_t*>(
        std::calloc(peak_queue_capacity_, sizeof(size_t)));
    if (!peak_values_ || !peak_indices_) {
        return ESP_ERR_NO_MEM;
    }
    memory_usage_bytes_ +=
        peak_queue_capacity_ * (sizeof(float) + sizeof(size_t));

    ceiling_db_ = kParameters[0].default_value;
    ceiling_linear_ = std::pow(10.0f, ceiling_db_ / 20.0f);
    release_ms_ = kParameters[1].default_value;
    update_release_coefficient();
    gain_ = 1.0f;
    initialize_mix(1.0f);
    return ESP_OK;
}

void Limiter::deinitialize_effect() noexcept {
    release_buffers();
}

void Limiter::reset_effect() noexcept {
    for (ChannelState& channel : channels_) {
        if (channel.samples) {
            std::memset(channel.samples, 0, channel.length * sizeof(float));
        }
        channel.write_position = 0;
    }
    if (peak_values_) {
        std::memset(peak_values_, 0,
                    peak_queue_capacity_ * sizeof(float));
    }
    if (peak_indices_) {
        std::memset(peak_indices_, 0,
                    peak_queue_capacity_ * sizeof(size_t));
    }
    peak_queue_head_ = 0;
    peak_queue_tail_ = 0;
    peak_queue_count_ = 0;
    frame_index_ = 0;
    gain_ = 1.0f;
}

void Limiter::process_frame(const float* input, float* wet_output) noexcept {
    const size_t channel_count = initialized_format().channels;
    float peak = 0.0f;
    for (size_t channel = 0; channel < channel_count; ++channel) {
        peak = std::max(peak, std::fabs(input[channel]));
    }

    const size_t first_frame =
        frame_index_ > lookahead_frames_
            ? frame_index_ - lookahead_frames_
            : 0;
    while (peak_queue_count_ > 0 &&
           peak_indices_[peak_queue_head_] < first_frame) {
        peak_queue_head_ = (peak_queue_head_ + 1) % peak_queue_capacity_;
        --peak_queue_count_;
    }
    while (peak_queue_count_ > 0) {
        const size_t last =
            (peak_queue_tail_ + peak_queue_capacity_ - 1) %
            peak_queue_capacity_;
        if (peak_values_[last] > peak) {
            break;
        }
        peak_queue_tail_ = last;
        --peak_queue_count_;
    }
    peak_values_[peak_queue_tail_] = peak;
    peak_indices_[peak_queue_tail_] = frame_index_;
    peak_queue_tail_ = (peak_queue_tail_ + 1) % peak_queue_capacity_;
    ++peak_queue_count_;

    const float maximum_peak = peak_values_[peak_queue_head_];
    const float target_gain =
        maximum_peak > ceiling_linear_ ? ceiling_linear_ / maximum_peak : 1.0f;
    if (target_gain < gain_) {
        gain_ = target_gain;
    } else {
        gain_ = release_coefficient_ * gain_ +
                (1.0f - release_coefficient_) * target_gain;
    }

    for (size_t channel_index = 0; channel_index < channel_count;
         ++channel_index) {
        ChannelState& channel = channels_[channel_index];
        const float delayed = channel.samples[channel.write_position];
        channel.samples[channel.write_position] = input[channel_index];
        wet_output[channel_index] = delayed * gain_;
        if (++channel.write_position == channel.length) {
            channel.write_position = 0;
        }
    }
    ++frame_index_;
}

void Limiter::release_buffers() noexcept {
    for (ChannelState& channel : channels_) {
        std::free(channel.samples);
        channel = {};
    }
    std::free(peak_values_);
    std::free(peak_indices_);
    peak_values_ = nullptr;
    peak_indices_ = nullptr;
    peak_queue_capacity_ = 0;
    peak_queue_head_ = 0;
    peak_queue_tail_ = 0;
    peak_queue_count_ = 0;
    lookahead_frames_ = 0;
    frame_index_ = 0;
    memory_usage_bytes_ = 0;
}

void Limiter::update_release_coefficient() noexcept {
    const uint32_t sample_rate_hz =
        initialized_format().sample_rate_hz;
    if (sample_rate_hz == 0) {
        return;
    }
    const float time_seconds = release_ms_ * 0.001f;
    release_coefficient_ =
        std::exp(-1.0f / (time_seconds * static_cast<float>(sample_rate_hz)));
}

}  // namespace audio_fx
