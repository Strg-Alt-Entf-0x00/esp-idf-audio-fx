#include "audio_fx/effect.hpp"

#include <cmath>
#include <limits>

namespace audio_fx {
namespace {

constexpr uint32_t kMinimumSampleRateHz = 8000;
constexpr uint32_t kMaximumSampleRateHz = 96000;
constexpr size_t kMixRampMilliseconds = 5;

bool valid_format(const AudioFormat& format) noexcept {
    return format.sample_rate_hz >= kMinimumSampleRateHz &&
           format.sample_rate_hz <= kMaximumSampleRateHz &&
           format.channels >= 1 && format.channels <= 2;
}

}  // namespace

esp_err_t Effect::initialize(const AudioFormat& format) noexcept {
    if (initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!valid_format(format)) {
        return ESP_ERR_INVALID_ARG;
    }

    format_ = format;
    const esp_err_t err = initialize_effect();
    if (err != ESP_OK) {
        deinitialize_effect();
        format_ = {};
        return err;
    }

    initialized_ = true;
    reset_effect();
    return ESP_OK;
}

esp_err_t Effect::process(const float* input, float* output,
                          size_t frame_count) noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame_count == 0) {
        return ESP_OK;
    }
    if (!input || !output ||
        frame_count > std::numeric_limits<size_t>::max() / format_.channels) {
        return ESP_ERR_INVALID_ARG;
    }

    float dry_frame[2] = {};
    float wet_frame[2] = {};
    const size_t channels = format_.channels;
    for (size_t frame = 0; frame < frame_count; ++frame) {
        const size_t offset = frame * channels;
        for (size_t channel = 0; channel < channels; ++channel) {
            dry_frame[channel] = input[offset + channel];
        }

        process_frame(dry_frame, wet_frame);
        const float wet_mix = advance_mix();
        const float dry_mix = 1.0f - wet_mix;
        for (size_t channel = 0; channel < channels; ++channel) {
            output[offset + channel] =
                dry_frame[channel] * dry_mix + wet_frame[channel] * wet_mix;
        }
    }
    return ESP_OK;
}

void Effect::reset() noexcept {
    if (initialized_) {
        reset_effect();
    }
}

void Effect::set_enabled(bool enabled) noexcept {
    enabled_ = enabled;
    update_mix_target();
}

bool Effect::is_enabled() const noexcept {
    return enabled_;
}

bool Effect::is_initialized() const noexcept {
    return initialized_;
}

const AudioFormat& Effect::format() const noexcept {
    return format_;
}

esp_err_t Effect::set_mix(float value) noexcept {
    if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    mix_value_ = value;
    update_mix_target();
    return ESP_OK;
}

void Effect::initialize_mix(float value) noexcept {
    mix_value_ = value;
    mix_current_ = enabled_ ? value : 0.0f;
    mix_target_ = mix_current_;
    mix_step_ = 0.0f;
    mix_ramp_remaining_ = 0;
}

float Effect::mix() const noexcept {
    return mix_value_;
}

const AudioFormat& Effect::initialized_format() const noexcept {
    return format_;
}

void Effect::update_mix_target() noexcept {
    if (!initialized_) {
        return;
    }

    mix_target_ = enabled_ ? mix_value_ : 0.0f;
    const size_t ramp_frames =
        (static_cast<size_t>(format_.sample_rate_hz) * kMixRampMilliseconds) /
        1000;
    mix_ramp_remaining_ = ramp_frames > 0 ? ramp_frames : 1;
    mix_step_ = (mix_target_ - mix_current_) /
                static_cast<float>(mix_ramp_remaining_);
}

float Effect::advance_mix() noexcept {
    if (mix_ramp_remaining_ > 0) {
        mix_current_ += mix_step_;
        --mix_ramp_remaining_;
        if (mix_ramp_remaining_ == 0) {
            mix_current_ = mix_target_;
        }
    }
    return mix_current_;
}

}  // namespace audio_fx
