idf_component_register(
    SRCS "test_audio_fx.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp-idf-audio-fx esp_timer unity
)

set_target_properties(${COMPONENT_LIB} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
