#include "audio_fx/effects/reverb.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace audio_fx {
namespace {

constexpr size_t kCombDelaySamples[8] = {
    1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617,
};
constexpr size_t kAllpassDelaySamples[4] = {
    225, 341, 441, 556,
};
constexpr size_t kStereoSpreadSamples = 23;
constexpr float kInputGain = 0.015f;
constexpr float kAllpassFeedback = 0.5f;
constexpr ParameterInfo kParameters[] = {
    {"mix", 0.0f, 1.0f, 0.25f},
    {"room_size", 0.0f, 1.0f, 0.7f},
    {"damping", 0.0f, 0.99f, 0.2f},
};

bool scaled_delay(size_t base, uint32_t sample_rate_hz, size_t spread,
                  size_t channel, size_t* result) noexcept {
    if (!result || base > SIZE_MAX / sample_rate_hz) {
        return false;
    }
    const size_t scaled = (base * sample_rate_hz) / 44100;
    if (scaled > SIZE_MAX - spread * channel) {
        return false;
    }
    *result = scaled + spread * channel;
    return *result > 0;
}

template <typename Line>
bool initialize_line(Line* line, size_t length, size_t* memory_usage) noexcept {
    if (!line || !memory_usage || length > SIZE_MAX / sizeof(float)) {
        return false;
    }
    line->samples = static_cast<float*>(std::calloc(length, sizeof(float)));
    if (!line->samples) {
        return false;
    }
    line->length = length;
    line->position = 0;
    *memory_usage += length * sizeof(float);
    return true;
}

}  // namespace

Reverb::~Reverb() {
    deinitialize_effect();
}

const char* Reverb::name() const noexcept {
    return "reverb";
}

size_t Reverb::parameter_count() const noexcept {
    return sizeof(kParameters) / sizeof(kParameters[0]);
}

const ParameterInfo* Reverb::parameters() const noexcept {
    return kParameters;
}

esp_err_t Reverb::set_parameter(size_t index, float value) noexcept {
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
            room_size_ = value;
            return ESP_OK;
        case 2:
            if (value < kParameters[2].minimum || value > kParameters[2].maximum) {
                return ESP_ERR_INVALID_ARG;
            }
            damping_ = value;
            return ESP_OK;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t Reverb::get_parameter(size_t index, float* value) const noexcept {
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (index) {
        case 0:
            *value = mix();
            return ESP_OK;
        case 1:
            *value = room_size_;
            return ESP_OK;
        case 2:
            *value = damping_;
            return ESP_OK;
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

size_t Reverb::memory_usage_bytes() const noexcept {
    return memory_usage_bytes_;
}

esp_err_t Reverb::initialize_effect() noexcept {
    const AudioFormat& audio_format = initialized_format();
    for (size_t channel = 0; channel < audio_format.channels; ++channel) {
        for (size_t index = 0; index < kCombCount; ++index) {
            size_t length = 0;
            if (!scaled_delay(kCombDelaySamples[index], audio_format.sample_rate_hz,
                              kStereoSpreadSamples, channel, &length) ||
                !initialize_line(&combs_[channel][index], length,
                                 &memory_usage_bytes_)) {
                return ESP_ERR_NO_MEM;
            }
        }
        for (size_t index = 0; index < kAllpassCount; ++index) {
            size_t length = 0;
            if (!scaled_delay(kAllpassDelaySamples[index],
                              audio_format.sample_rate_hz,
                              kStereoSpreadSamples, channel, &length) ||
                !initialize_line(&allpasses_[channel][index], length,
                                 &memory_usage_bytes_)) {
                return ESP_ERR_NO_MEM;
            }
        }
    }
    room_size_ = kParameters[1].default_value;
    damping_ = kParameters[2].default_value;
    initialize_mix(kParameters[0].default_value);
    return ESP_OK;
}

void Reverb::deinitialize_effect() noexcept {
    release_lines();
}

void Reverb::reset_effect() noexcept {
    for (size_t channel = 0; channel < kMaxChannels; ++channel) {
        for (size_t index = 0; index < kCombCount; ++index) {
            CombLine& line = combs_[channel][index];
            if (line.samples) {
                std::memset(line.samples, 0, line.length * sizeof(float));
            }
            line.position = 0;
            line.filter_store = 0.0f;
        }
        for (size_t index = 0; index < kAllpassCount; ++index) {
            DelayLine& line = allpasses_[channel][index];
            if (line.samples) {
                std::memset(line.samples, 0, line.length * sizeof(float));
            }
            line.position = 0;
        }
    }
}

void Reverb::process_frame(const float* input, float* wet_output) noexcept {
    const size_t channel_count = initialized_format().channels;
    const float feedback = 0.7f + room_size_ * 0.28f;
    for (size_t channel = 0; channel < channel_count; ++channel) {
        float reverberated = 0.0f;
        for (size_t index = 0; index < kCombCount; ++index) {
            CombLine& line = combs_[channel][index];
            const float delayed = line.samples[line.position];
            line.filter_store =
                delayed * (1.0f - damping_) + line.filter_store * damping_;
            line.samples[line.position] = input[channel] * kInputGain +
                                          line.filter_store * feedback;
            reverberated += delayed;
            if (++line.position == line.length) {
                line.position = 0;
            }
        }

        for (size_t index = 0; index < kAllpassCount; ++index) {
            DelayLine& line = allpasses_[channel][index];
            const float delayed = line.samples[line.position];
            const float output = -reverberated + delayed;
            line.samples[line.position] =
                reverberated + delayed * kAllpassFeedback;
            reverberated = output;
            if (++line.position == line.length) {
                line.position = 0;
            }
        }
        wet_output[channel] = reverberated;
    }
}

void Reverb::release_lines() noexcept {
    for (size_t channel = 0; channel < kMaxChannels; ++channel) {
        for (size_t index = 0; index < kCombCount; ++index) {
            std::free(combs_[channel][index].samples);
            combs_[channel][index] = {};
        }
        for (size_t index = 0; index < kAllpassCount; ++index) {
            std::free(allpasses_[channel][index].samples);
            allpasses_[channel][index] = {};
        }
    }
    memory_usage_bytes_ = 0;
}

}  // namespace audio_fx
