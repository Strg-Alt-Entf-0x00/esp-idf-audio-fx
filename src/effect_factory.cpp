#include "audio_fx/effect_factory.hpp"

#include <new>
#include <utility>

#include "audio_fx/effects/bitcrusher.hpp"
#include "audio_fx/effects/chorus.hpp"
#include "audio_fx/effects/compressor.hpp"
#include "audio_fx/effects/delay.hpp"
#include "audio_fx/effects/equalizer.hpp"
#include "audio_fx/effects/flanger.hpp"
#include "audio_fx/effects/limiter.hpp"
#include "audio_fx/effects/reverb.hpp"

namespace audio_fx {

esp_err_t create_effect(EffectType type, const AudioFormat& format,
                        std::unique_ptr<Effect>* effect) noexcept {
    if (!effect) {
        return ESP_ERR_INVALID_ARG;
    }

    std::unique_ptr<Effect> candidate;
    switch (type) {
        case EffectType::reverb:
#if CONFIG_AUDIO_FX_ENABLE_REVERB
            candidate.reset(new (std::nothrow) Reverb());
#else
            return ESP_ERR_NOT_SUPPORTED;
#endif
            break;
        case EffectType::chorus:
#if CONFIG_AUDIO_FX_ENABLE_CHORUS
            candidate.reset(new (std::nothrow) Chorus());
#else
            return ESP_ERR_NOT_SUPPORTED;
#endif
            break;
        case EffectType::equalizer:
#if CONFIG_AUDIO_FX_ENABLE_EQUALIZER
            candidate.reset(new (std::nothrow) Equalizer());
#else
            return ESP_ERR_NOT_SUPPORTED;
#endif
            break;
        case EffectType::delay:
#if CONFIG_AUDIO_FX_ENABLE_DELAY
            candidate.reset(new (std::nothrow) Delay());
#else
            return ESP_ERR_NOT_SUPPORTED;
#endif
            break;
        case EffectType::flanger:
#if CONFIG_AUDIO_FX_ENABLE_FLANGER
            candidate.reset(new (std::nothrow) Flanger());
#else
            return ESP_ERR_NOT_SUPPORTED;
#endif
            break;
        case EffectType::bitcrusher:
#if CONFIG_AUDIO_FX_ENABLE_BITCRUSHER
            candidate.reset(new (std::nothrow) Bitcrusher());
#else
            return ESP_ERR_NOT_SUPPORTED;
#endif
            break;
        case EffectType::compressor:
#if CONFIG_AUDIO_FX_ENABLE_COMPRESSOR
            candidate.reset(new (std::nothrow) Compressor());
#else
            return ESP_ERR_NOT_SUPPORTED;
#endif
            break;
        case EffectType::limiter:
#if CONFIG_AUDIO_FX_ENABLE_LIMITER
            candidate.reset(new (std::nothrow) Limiter());
#else
            return ESP_ERR_NOT_SUPPORTED;
#endif
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    if (!candidate) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t err = candidate->initialize(format);
    if (err != ESP_OK) {
        return err;
    }
    *effect = std::move(candidate);
    return ESP_OK;
}

}  // namespace audio_fx
