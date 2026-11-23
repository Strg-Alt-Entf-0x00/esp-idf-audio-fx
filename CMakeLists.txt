set(audio_fx_sources
    "src/effect.cpp"
    "src/effect_factory.cpp"
)

if(CONFIG_AUDIO_FX_ENABLE_REVERB)
    list(APPEND audio_fx_sources "src/effects/reverb.cpp")
endif()

if(CONFIG_AUDIO_FX_ENABLE_CHORUS)
    list(APPEND audio_fx_sources "src/effects/chorus.cpp")
endif()

if(CONFIG_AUDIO_FX_ENABLE_EQUALIZER)
    list(APPEND audio_fx_sources "src/effects/equalizer.cpp")
endif()

if(CONFIG_AUDIO_FX_ENABLE_DELAY)
    list(APPEND audio_fx_sources "src/effects/delay.cpp")
endif()

if(CONFIG_AUDIO_FX_ENABLE_FLANGER)
    list(APPEND audio_fx_sources "src/effects/flanger.cpp")
endif()

if(CONFIG_AUDIO_FX_ENABLE_BITCRUSHER)
    list(APPEND audio_fx_sources "src/effects/bitcrusher.cpp")
endif()

if(CONFIG_AUDIO_FX_ENABLE_COMPRESSOR)
    list(APPEND audio_fx_sources "src/effects/compressor.cpp")
endif()

if(CONFIG_AUDIO_FX_ENABLE_LIMITER)
    list(APPEND audio_fx_sources "src/effects/limiter.cpp")
endif()

idf_component_register(
    SRCS ${audio_fx_sources}
    INCLUDE_DIRS "include"
    REQUIRES esp_common
)

set_target_properties(${COMPONENT_LIB} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
