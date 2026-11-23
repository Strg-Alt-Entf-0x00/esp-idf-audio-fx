cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../.."
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(audio_fx_console_audio_fx)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(AUDIO_DATA_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.data")
set(STAGED_AUDIO_DIR "${CMAKE_BINARY_DIR}/wav_player_data")
file(MAKE_DIRECTORY "${STAGED_AUDIO_DIR}/fs0")
file(MAKE_DIRECTORY "${STAGED_AUDIO_DIR}/fs1")

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/tools-py/stage_audio.py" AND EXISTS "${AUDIO_DATA_DIR}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_LIST_DIR}/tools-py/stage_audio.py"
                --source "${AUDIO_DATA_DIR}"
                --destination "${STAGED_AUDIO_DIR}"
        RESULT_VARIABLE STAGE_AUDIO_RESULT
        OUTPUT_VARIABLE STAGE_AUDIO_OUTPUT
        ERROR_VARIABLE STAGE_AUDIO_ERROR
    )
    if(NOT STAGE_AUDIO_RESULT EQUAL 0)
        message(WARNING "Local WAV staging failed:\n${STAGE_AUDIO_OUTPUT}${STAGE_AUDIO_ERROR}")
    else()
        message(STATUS "${STAGE_AUDIO_OUTPUT}")
    endif()
endif()

spiffs_create_partition_image(audio0 "${STAGED_AUDIO_DIR}/fs0" FLASH_IN_PROJECT)
spiffs_create_partition_image(audio1 "${STAGED_AUDIO_DIR}/fs1" FLASH_IN_PROJECT)
