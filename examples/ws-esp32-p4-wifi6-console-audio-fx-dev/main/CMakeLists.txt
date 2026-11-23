idf_component_register(
    SRCS "audio_fx_app.cpp" "wav_reader.cpp" "audio_board.cpp"
    INCLUDE_DIRS "."
    REQUIRES console esp-idf-audio-fx spiffs espressif__esp_codec_dev
    PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_i2s
)

set_target_properties(${COMPONENT_LIB} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
