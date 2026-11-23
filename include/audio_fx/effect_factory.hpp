#pragma once

#include <memory>

#include "audio_fx/effect.hpp"

namespace audio_fx {

enum class EffectType {
    reverb,
    chorus,
    equalizer,
    delay,
    flanger,
    bitcrusher,
    compressor,
    limiter,
};

esp_err_t create_effect(EffectType type, const AudioFormat& format,
                        std::unique_ptr<Effect>* effect) noexcept;

}  // namespace audio_fx
