#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "audio_board.hpp"
#include "audio_fx/audio_fx.hpp"
#include "esp_check.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "wav_reader.hpp"

namespace {

constexpr char kTag[] = "audio_fx_console";
constexpr char kFirmwareIdentity[] =
    "TOOL_ID:Strg-Alt-Entf-0x00/esp-idf-audio-fx:console_audio_fx:1";
constexpr uint32_t kSampleRateHz = 48000;
constexpr size_t kStereoChannels = 2;
constexpr size_t kBlockFrames = 240;
constexpr size_t kMaximumTracks = 32;
constexpr size_t kQueueLength = 12;
constexpr uint32_t kReadTimeoutMs = 50;
constexpr uint32_t kWriteTimeoutMs = 100;
constexpr uint32_t kDmaDrainTimeoutMs = 500;
constexpr uint8_t kDryEffect = 8;

constexpr audio_fx::EffectType kEffectTypes[kDryEffect] = {
    audio_fx::EffectType::reverb,
    audio_fx::EffectType::chorus,
    audio_fx::EffectType::equalizer,
    audio_fx::EffectType::delay,
    audio_fx::EffectType::flanger,
    audio_fx::EffectType::bitcrusher,
    audio_fx::EffectType::compressor,
    audio_fx::EffectType::limiter,
};
constexpr const char* kEffectNames[kDryEffect + 1] = {
    "reverb", "chorus", "equalizer", "delay", "flanger", "bitcrusher",
    "compressor", "limiter", "dry",
};

struct Track {
    uint8_t filesystem_index = 0;
    char file_name[16]{};
    char display_name[224]{};
};

enum class AudioMode : uint8_t {
    stopped,
    wav,
    microphone,
};

enum class CommandType : uint8_t {
    play,
    stop,
    start_live,
    select_microphone,
    set_microphone_gain,
    select_effect,
    list_parameters,
    get_bypass,
    set_parameter,
    set_bypass,
    set_volume,
};

struct AudioCommand {
    CommandType type = CommandType::stop;
    uint8_t effect_index = 0;
    size_t track_index = 0;
    size_t parameter_index = 0;
    float value = 0.0f;
    TaskHandle_t reply_task = nullptr;
};

struct SignalMetrics {
    uint64_t samples = 0;
    uint64_t sum_squares = 0;
    uint32_t peak_sample = 0;

    void add(float sample) {
        const float bounded = std::clamp(sample, -1.0f, 1.0f);
        const int32_t quantized = static_cast<int32_t>(bounded * 32767.0f);
        const uint32_t magnitude = static_cast<uint32_t>(
            quantized < 0 ? -quantized : quantized);
        ++samples;
        sum_squares += static_cast<uint64_t>(magnitude) * magnitude;
        peak_sample = std::max(peak_sample, magnitude);
    }

    float rms_dbfs() const {
        if (samples == 0 || sum_squares == 0) {
            return -120.0f;
        }
        const double mean_square =
            static_cast<double>(sum_squares) /
            (static_cast<double>(samples) * 32767.0 * 32767.0);
        return static_cast<float>(10.0 * std::log10(mean_square));
    }

    float peak_dbfs() const {
        if (peak_sample == 0) {
            return -120.0f;
        }
        return 20.0f * std::log10(static_cast<float>(peak_sample) / 32767.0f);
    }
};

struct StreamDiagnostics {
    uint64_t frames_read = 0;
    uint64_t bytes_written = 0;
    uint32_t invalid_samples = 0;
    uint32_t clipped_samples = 0;
    uint32_t write_errors = 0;
    esp_err_t last_write_error = ESP_OK;
    uint32_t dma_completions_at_start = 0;
    uint32_t dma_non_silent_at_start = 0;
    uint32_t dma_silent_at_start = 0;
    uint32_t dma_bytes_at_start = 0;
    bool has_dma_baseline = false;
    SignalMetrics source;
    SignalMetrics processed;
};

struct PlaybackState {
    audio_fx_demo::WavReader reader;
    std::unique_ptr<audio_fx::Effect> effect;
    AudioMode mode = AudioMode::stopped;
    waveshare_audio::MicrophoneInput microphone =
        waveshare_audio::MicrophoneInput::inmp441;
    uint8_t effect_index = kDryEffect;
    size_t track_index = 0;
    StreamDiagnostics diagnostics;
};

std::array<Track, kMaximumTracks> s_tracks{};
size_t s_track_count = 0;
QueueHandle_t s_commands = nullptr;
std::atomic<AudioMode> s_audio_mode{AudioMode::stopped};
std::atomic<size_t> s_current_track{0};
std::atomic<uint8_t> s_requested_effect{kDryEffect};
std::atomic<uint8_t> s_requested_microphone{
    static_cast<uint8_t>(waveshare_audio::MicrophoneInput::inmp441)};
std::atomic<float> s_requested_volume_db{-30.0f};
std::atomic<float> s_requested_microphone_gain_db{0.0f};

bool parse_finite_float(const char* text, float* value) {
    if (!text || !value) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }
    *value = parsed;
    return true;
}

bool parse_index(const char* text, size_t* value) {
    if (!text || !value || *text == '\0' || *text == '-') {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed > SIZE_MAX) {
        return false;
    }
    *value = static_cast<size_t>(parsed);
    return true;
}

int enqueue_command(const AudioCommand& command) {
    if (!s_commands) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_commands, &command, 0) != pdTRUE) {
        ESP_LOGW(kTag, "Audio command queue is full; command was not accepted");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

int enqueue_command_and_wait(AudioCommand command) {
    command.reply_task = xTaskGetCurrentTaskHandle();
    const int error = enqueue_command(command);
    if (error != ESP_OK) {
        return error;
    }
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) == 0) {
        ESP_LOGE(kTag, "Timed out waiting for the audio task");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t load_tracks() {
    FILE* manifest = std::fopen("/audio0/tracks.tsv", "r");
    if (!manifest) {
        ESP_LOGE(kTag, "Could not open SPIFFS track manifest");
        return ESP_ERR_NOT_FOUND;
    }

    char line[256]{};
    size_t line_number = 0;
    while (std::fgets(line, sizeof(line), manifest)) {
        ++line_number;
        const size_t length = std::strlen(line);
        if (length == 0 || line[length - 1] != '\n') {
            std::fclose(manifest);
            ESP_LOGE(kTag, "Malformed track manifest line %u",
                     static_cast<unsigned>(line_number));
            return ESP_ERR_INVALID_SIZE;
        }
        line[length - 1] = '\0';
        char* first_separator = std::strchr(line, '\t');
        char* second_separator =
            first_separator ? std::strchr(first_separator + 1, '\t') : nullptr;
        if (!first_separator || first_separator == line ||
            !second_separator || second_separator[1] == '\0' ||
            s_track_count >= s_tracks.size()) {
            std::fclose(manifest);
            ESP_LOGE(kTag, "Invalid track manifest entry at line %u",
                     static_cast<unsigned>(line_number));
            return ESP_ERR_INVALID_SIZE;
        }
        *first_separator = '\0';
        *second_separator = '\0';
        const size_t file_name_length =
            std::strlen(first_separator + 1);
        const size_t display_name_length =
            std::strlen(second_separator + 1);
        uint8_t filesystem_index = 0;
        if (std::strcmp(line, "audio0") == 0) {
            filesystem_index = 0;
        } else if (std::strcmp(line, "audio1") == 0) {
            filesystem_index = 1;
        } else {
            std::fclose(manifest);
            ESP_LOGE(kTag, "Invalid filesystem in track manifest");
            return ESP_ERR_INVALID_SIZE;
        }
        if (file_name_length >= sizeof(Track::file_name) ||
            display_name_length >= sizeof(Track::display_name) ||
            std::strncmp(first_separator + 1, "track_", 6) != 0 ||
            file_name_length < 10 ||
            std::strcmp(first_separator + 1 + file_name_length - 4,
                        ".wav") != 0 ||
            std::strstr(first_separator + 1, "..") != nullptr ||
            std::strchr(first_separator + 1, '/') != nullptr ||
            std::strchr(first_separator + 1, '\\') != nullptr) {
            std::fclose(manifest);
            ESP_LOGE(kTag, "Unsafe or oversized track manifest entry");
            return ESP_ERR_INVALID_SIZE;
        }

        Track& track = s_tracks[s_track_count++];
        track.filesystem_index = filesystem_index;
        std::memcpy(track.file_name, first_separator + 1,
                    file_name_length + 1);
        std::memcpy(track.display_name, second_separator + 1,
                    display_name_length + 1);
    }
    const bool read_failed = std::ferror(manifest) != 0;
    std::fclose(manifest);
    if (read_failed) {
        ESP_LOGE(kTag, "Failed while reading SPIFFS track manifest");
        return ESP_FAIL;
    }
    ESP_LOGI(kTag, "Loaded %u WAV track(s) from SPIFFS",
             static_cast<unsigned>(s_track_count));
    return ESP_OK;
}

int tool_id_command(int argc, char**) {
    if (argc != 1) {
        std::printf("Usage: tool_id\n");
        return 1;
    }
    std::printf("%s\n", kFirmwareIdentity);
    return 0;
}

int tracks_command(int argc, char**) {
    if (argc != 1) {
        std::printf("Usage: tracks\n");
        return 1;
    }
    if (s_track_count == 0) {
        std::printf("No WAV tracks staged. Add licensed WAV files to .data and rebuild.\n");
        return 0;
    }
    for (size_t index = 0; index < s_track_count; ++index) {
        std::printf("TRACK index=%u name=%s\n",
                    static_cast<unsigned>(index + 1),
                    s_tracks[index].display_name);
    }
    return 0;
}

int play_command(int argc, char** argv) {
    size_t one_based_index = 0;
    if (argc != 2 || !parse_index(argv[1], &one_based_index) ||
        one_based_index == 0 || one_based_index > s_track_count) {
        std::printf("Usage: play <track-number from 'tracks'>\n");
        return 1;
    }
    AudioCommand command{};
    command.type = CommandType::play;
    command.track_index = one_based_index - 1;
    return enqueue_command_and_wait(command);
}

int stop_command(int argc, char**) {
    if (argc != 1) {
        std::printf("Usage: stop\n");
        return 1;
    }
    AudioCommand command{};
    command.type = CommandType::stop;
    return enqueue_command_and_wait(command);
}

int audio_command(int argc, char** argv) {
    if (argc != 2) {
        std::printf("Usage: audio start|stop|status\n");
        return 1;
    }
    if (std::strcmp(argv[1], "status") == 0) {
        const AudioMode mode = s_audio_mode.load();
        if (mode == AudioMode::microphone) {
            std::printf("Audio stream: live microphone input\n");
        } else if (mode == AudioMode::wav) {
            std::printf("Audio stream: WAV track %u\n",
                        static_cast<unsigned>(s_current_track.load() + 1));
        } else {
            std::printf("Audio stream: stopped\n");
        }
        return 0;
    }
    if (std::strcmp(argv[1], "start") == 0) {
        AudioCommand command{};
        command.type = CommandType::start_live;
        return enqueue_command_and_wait(command);
    }
    if (std::strcmp(argv[1], "stop") == 0) {
        AudioCommand command{};
        command.type = CommandType::stop;
        return enqueue_command_and_wait(command);
    }
    std::printf("Usage: audio start|stop|status\n");
    return 1;
}

int status_command(int argc, char**) {
    if (argc != 1) {
        std::printf("Usage: status\n");
        return 1;
    }
    const AudioMode mode = s_audio_mode.load();
    if (mode == AudioMode::microphone) {
        const char* microphone =
            s_requested_microphone.load() ==
                    static_cast<uint8_t>(waveshare_audio::MicrophoneInput::codec)
                ? "codec"
                : "inmp441";
        std::printf("Live microphone: %s; effect: %s; volume: %.1f dB\n",
                    microphone, kEffectNames[s_requested_effect.load()],
                    s_requested_volume_db.load());
    } else if (mode == AudioMode::wav) {
        const size_t index = s_current_track.load();
        std::printf("Playing track %u: %s; effect: %s; volume: %.1f dB\n",
                    static_cast<unsigned>(index + 1),
                    s_tracks[index].display_name,
                    kEffectNames[s_requested_effect.load()],
                    s_requested_volume_db.load());
    } else {
        std::printf("Playback stopped; selected effect: %s; volume: %.1f dB\n",
                    kEffectNames[s_requested_effect.load()],
                    s_requested_volume_db.load());
    }
    return 0;
}

int diagnostics_command(int argc, char**) {
    if (argc != 1) {
        std::printf("Usage: diag\n");
        return 1;
    }
    waveshare_audio::OutputDiagnostics diagnostics{};
    waveshare_audio::get_output_diagnostics(&diagnostics);
    std::printf(
        "DMA completed=%u bytes=%u non_silent=%u silent=%u peak_sample=%u "
        "buffer_bytes=%u\n",
        static_cast<unsigned>(diagnostics.dma_completions),
        static_cast<unsigned>(diagnostics.dma_bytes),
        static_cast<unsigned>(diagnostics.dma_non_silent_buffers),
        static_cast<unsigned>(diagnostics.dma_silent_buffers),
        static_cast<unsigned>(diagnostics.dma_peak_sample),
        static_cast<unsigned>(diagnostics.dma_buffer_bytes));
    return 0;
}

int effect_command(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "list") == 0) {
        for (size_t index = 0; index <= kDryEffect; ++index) {
            std::printf("%s%s\n",
                        index == s_requested_effect.load() ? "* " : "  ",
                        kEffectNames[index]);
        }
        std::printf("Use 'fx select <effect>' and 'fx params' for parameter details.\n");
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "params") == 0) {
        AudioCommand command{};
        command.type = CommandType::list_parameters;
        command.effect_index = s_requested_effect.load();
        command.reply_task = xTaskGetCurrentTaskHandle();
        const int error = enqueue_command(command);
        if (error != ESP_OK) {
            return error;
        }
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)) == 0) {
            ESP_LOGE(kTag, "Timed out waiting for effect parameters");
            return ESP_ERR_TIMEOUT;
        }
        return 0;
    }
    if (argc == 3 && std::strcmp(argv[1], "select") == 0) {
        uint8_t selected_index = kDryEffect + 1;
        for (uint8_t index = 0; index <= kDryEffect; ++index) {
            if (std::strcmp(argv[2], kEffectNames[index]) == 0 ||
                (index == 2 && std::strcmp(argv[2], "eq") == 0)) {
                selected_index = index;
                break;
            }
        }
        if (selected_index > kDryEffect) {
            std::printf("Unknown effect. Use 'fx list' for available effects.\n");
            return 1;
        }
        AudioCommand command{};
        command.type = CommandType::select_effect;
        command.effect_index = selected_index;
        return enqueue_command_and_wait(command);
    }
    if (argc == 4 && std::strcmp(argv[1], "set") == 0) {
        size_t parameter_index = 0;
        float value = 0.0f;
        if (!parse_index(argv[2], &parameter_index) ||
            !parse_finite_float(argv[3], &value)) {
            std::printf("Usage: fx set <parameter-index> <finite-value>\n");
            return 1;
        }
        AudioCommand command{};
        command.type = CommandType::set_parameter;
        command.effect_index = s_requested_effect.load();
        command.parameter_index = parameter_index;
        command.value = value;
        return enqueue_command_and_wait(command);
    }
    if (argc == 3 && std::strcmp(argv[1], "bypass") == 0) {
        if (std::strcmp(argv[2], "status") == 0) {
            AudioCommand command{};
            command.type = CommandType::get_bypass;
            command.effect_index = s_requested_effect.load();
            command.reply_task = xTaskGetCurrentTaskHandle();
            const int error = enqueue_command(command);
            if (error != ESP_OK) {
                return error;
            }
            if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)) == 0) {
                ESP_LOGE(kTag, "Timed out waiting for bypass state");
                return ESP_ERR_TIMEOUT;
            }
            return 0;
        }
        if (std::strcmp(argv[2], "on") != 0 &&
            std::strcmp(argv[2], "off") != 0) {
            std::printf("Usage: fx bypass status|on|off\n");
            return 1;
        }
        AudioCommand command{};
        command.type = CommandType::set_bypass;
        command.effect_index = s_requested_effect.load();
        command.value = std::strcmp(argv[2], "on") == 0 ? 1.0f : 0.0f;
        return enqueue_command_and_wait(command);
    }
    std::printf("Usage: fx list|params|select <effect>|set <index> <value>|bypass status|on|off\n");
    return 1;
}

int volume_command(int argc, char** argv) {
    if (argc == 1) {
        std::printf("Speaker volume: %.1f dB\n", s_requested_volume_db.load());
        return 0;
    }
    float value = 0.0f;
    if (argc != 2 || !parse_finite_float(argv[1], &value) ||
        value < -60.0f || value > 0.0f) {
        std::printf("Usage: volume [<-60..0 dB>]\n");
        return 1;
    }
    AudioCommand command{};
    command.type = CommandType::set_volume;
    command.value = value;
    return enqueue_command_and_wait(command);
}

int microphone_select_command(int argc, char** argv) {
    if (argc == 1) {
        std::printf("MIC_CONFIG input=%s\n",
                    s_requested_microphone.load() ==
                            static_cast<uint8_t>(
                                waveshare_audio::MicrophoneInput::codec)
                        ? "codec"
                        : "inmp441");
        return 0;
    }
    if (argc != 2) {
        std::printf("Usage: mic_select [codec|inmp441]\n");
        return 1;
    }
    AudioCommand command{};
    command.type = CommandType::select_microphone;
    if (std::strcmp(argv[1], "codec") == 0) {
        command.effect_index =
            static_cast<uint8_t>(waveshare_audio::MicrophoneInput::codec);
    } else if (std::strcmp(argv[1], "inmp441") == 0) {
        command.effect_index =
            static_cast<uint8_t>(waveshare_audio::MicrophoneInput::inmp441);
    } else {
        std::printf("Microphone must be codec or inmp441\n");
        return 1;
    }
    return enqueue_command_and_wait(command);
}

int microphone_gain_command(int argc, char** argv) {
    if (argc == 1) {
        std::printf("Microphone gain: %.1f dB\n",
                    s_requested_microphone_gain_db.load());
        return 0;
    }
    float value = 0.0f;
    if (argc != 2 || !parse_finite_float(argv[1], &value) ||
        value < 0.0f || value > 42.0f) {
        std::printf("Usage: mic_gain [0..42 dB]\n");
        return 1;
    }
    AudioCommand command{};
    command.type = CommandType::set_microphone_gain;
    command.value = value;
    return enqueue_command_and_wait(command);
}

esp_err_t register_command(const char* name, const char* help,
                           esp_console_cmd_func_t function) {
    esp_console_cmd_t definition{};
    definition.command = name;
    definition.help = help;
    definition.func = function;
    return esp_console_cmd_register(&definition);
}

esp_err_t initialize_console() {
    esp_console_repl_config_t repl_config =
        ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    const esp_console_dev_uart_config_t uart_config =
        ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_console_repl_t* repl = nullptr;
    ESP_RETURN_ON_ERROR(esp_console_new_repl_uart(
                            &uart_config, &repl_config, &repl),
                        kTag, "Failed to create UART console");
    ESP_RETURN_ON_ERROR(register_command("tool_id", "Report desktop-tool firmware identity",
                                         tool_id_command),
                        kTag, "Failed to register tool identity command");
    ESP_RETURN_ON_ERROR(register_command("tracks", "List local WAV tracks",
                                         tracks_command),
                        kTag, "Failed to register tracks command");
    ESP_RETURN_ON_ERROR(register_command("play", "Play a numbered WAV track",
                                         play_command),
                        kTag, "Failed to register play command");
    ESP_RETURN_ON_ERROR(register_command("stop", "Stop audio playback",
                                         stop_command),
                        kTag, "Failed to register stop command");
    ESP_RETURN_ON_ERROR(register_command("audio", "Start, stop, or inspect live audio",
                                         audio_command),
                        kTag, "Failed to register audio command");
    ESP_RETURN_ON_ERROR(register_command("status", "Show player status",
                                         status_command),
                        kTag, "Failed to register status command");
    ESP_RETURN_ON_ERROR(register_command("diag", "Show I2S DMA diagnostics",
                                         diagnostics_command),
                        kTag, "Failed to register diag command");
    ESP_RETURN_ON_ERROR(register_command("fx", "Select and control an effect",
                                         effect_command),
                        kTag, "Failed to register effect command");
    ESP_RETURN_ON_ERROR(register_command("volume", "Set speaker volume in dB",
                                         volume_command),
                        kTag, "Failed to register volume command");
    ESP_RETURN_ON_ERROR(register_command("mic_select", "Select codec or INMP441 microphone",
                                         microphone_select_command),
                        kTag, "Failed to register microphone selection");
    ESP_RETURN_ON_ERROR(register_command("mic_gain", "Set ES8311 microphone gain in dB",
                                         microphone_gain_command),
                        kTag, "Failed to register microphone gain");
    return esp_console_start_repl(repl);
}

void report_stream_diagnostics(PlaybackState* state, const char* reason,
                               bool wait_for_dma) {
    const AudioMode mode = state->mode;
    if (mode == AudioMode::stopped) {
        return;
    }

    if (mode == AudioMode::wav && wait_for_dma &&
        state->diagnostics.has_dma_baseline) {
        const TickType_t deadline =
            xTaskGetTickCount() + pdMS_TO_TICKS(kDmaDrainTimeoutMs);
        waveshare_audio::OutputDiagnostics output{};
        do {
            waveshare_audio::get_output_diagnostics(&output);
            const uint32_t completed_bytes =
                output.dma_bytes - state->diagnostics.dma_bytes_at_start;
            const uint32_t extra_bytes = 2 * output.dma_buffer_bytes;
            if (completed_bytes >=
                state->diagnostics.bytes_written + extra_bytes) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        } while (static_cast<int32_t>(deadline - xTaskGetTickCount()) > 0);
    }

    waveshare_audio::OutputDiagnostics output{};
    waveshare_audio::get_output_diagnostics(&output);
    const uint32_t dma_bytes = state->diagnostics.has_dma_baseline
        ? output.dma_bytes - state->diagnostics.dma_bytes_at_start
        : 0;
    const uint32_t dma_buffers = state->diagnostics.has_dma_baseline
        ? output.dma_completions -
              state->diagnostics.dma_completions_at_start
        : 0;
    const uint32_t non_silent_buffers = state->diagnostics.has_dma_baseline
        ? output.dma_non_silent_buffers -
              state->diagnostics.dma_non_silent_at_start
        : 0;
    const uint32_t silent_buffers = state->diagnostics.has_dma_baseline
        ? output.dma_silent_buffers - state->diagnostics.dma_silent_at_start
        : 0;
    const bool dma_drained =
        mode != AudioMode::wav || !wait_for_dma ||
        state->diagnostics.bytes_written == 0 ||
        dma_bytes >= state->diagnostics.bytes_written +
                         2 * output.dma_buffer_bytes;
    const bool has_signal = state->diagnostics.source.peak_sample > 0 &&
        state->diagnostics.processed.peak_sample > 0 &&
        (mode == AudioMode::microphone || output.dma_peak_sample > 0);
    const bool passed = std::strcmp(reason, "eof") == 0 &&
        mode == AudioMode::wav && state->diagnostics.frames_read > 0 &&
        state->diagnostics.bytes_written ==
            state->diagnostics.frames_read * kStereoChannels * sizeof(int16_t) &&
        state->diagnostics.invalid_samples == 0 &&
        state->diagnostics.write_errors == 0 && non_silent_buffers > 0 &&
        has_signal && dma_drained;
    const char* result = passed
        ? "PASS"
        : ((std::strcmp(reason, "stopped") == 0 ||
            std::strcmp(reason, "replaced") == 0)
               ? "INFO"
               : "FAIL");
    const char* stream_name = mode == AudioMode::microphone
        ? "microphone"
        : (state->track_index < s_track_count
               ? s_tracks[state->track_index].display_name
               : "unknown");
    const float dma_peak_dbfs = output.dma_peak_sample == 0
        ? -120.0f
        : 20.0f * std::log10(output.dma_peak_sample / 32767.0f);

    ESP_LOGI(kTag,
             "[AUDIO_DIAG] result=%s reason=%s source=%s effect=%s "
             "frames=%llu source_rms_dbfs=%.1f source_peak_dbfs=%.1f "
             "dsp_rms_dbfs=%.1f dsp_peak_dbfs=%.1f invalid_samples=%u "
             "clipped_samples=%u write_errors=%u last_write_error=%s "
             "i2s_write_bytes=%llu dma_buffers=%u dma_bytes=%u "
             "dma_non_silent=%u dma_silent=%u dma_peak_dbfs=%.1f "
             "dma_drain=%s",
             result, reason, stream_name, kEffectNames[state->effect_index],
             static_cast<unsigned long long>(state->diagnostics.frames_read),
             state->diagnostics.source.rms_dbfs(),
             state->diagnostics.source.peak_dbfs(),
             state->diagnostics.processed.rms_dbfs(),
             state->diagnostics.processed.peak_dbfs(),
             static_cast<unsigned>(state->diagnostics.invalid_samples),
             static_cast<unsigned>(state->diagnostics.clipped_samples),
             static_cast<unsigned>(state->diagnostics.write_errors),
             esp_err_to_name(state->diagnostics.last_write_error),
             static_cast<unsigned long long>(state->diagnostics.bytes_written),
             static_cast<unsigned>(dma_buffers),
             static_cast<unsigned>(dma_bytes),
             static_cast<unsigned>(non_silent_buffers),
             static_cast<unsigned>(silent_buffers), dma_peak_dbfs,
             dma_drained ? "yes" : "no");
}

void reset_stream_diagnostics(PlaybackState* state) {
    state->diagnostics = {};
    waveshare_audio::reset_output_diagnostics();
}

void close_stream(PlaybackState* state) {
    if (state->mode == AudioMode::wav) {
        state->reader.close();
    }
    state->mode = AudioMode::stopped;
    s_audio_mode.store(AudioMode::stopped);
}

void stop_stream(PlaybackState* state, bool report) {
    const AudioMode previous_mode = state->mode;
    if (previous_mode != AudioMode::stopped && report) {
        report_stream_diagnostics(state, "stopped", false);
    }
    close_stream(state);
    const esp_err_t error = previous_mode == AudioMode::microphone
        ? waveshare_audio::stop()
        : waveshare_audio::stop_output();
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Could not stop audio output: %s",
                 esp_err_to_name(error));
    }
}

void select_effect(PlaybackState* state, uint8_t effect_index) {
    if (effect_index == kDryEffect) {
        state->effect.reset();
        state->effect_index = kDryEffect;
        s_requested_effect.store(kDryEffect);
        ESP_LOGI(kTag, "Selected dry output");
        return;
    }
    if (effect_index >= kDryEffect) {
        ESP_LOGE(kTag, "Invalid effect selection %u",
                 static_cast<unsigned>(effect_index));
        return;
    }

    std::unique_ptr<audio_fx::Effect> candidate;
    const audio_fx::AudioFormat format{kSampleRateHz, kStereoChannels};
    const esp_err_t error = audio_fx::create_effect(
        kEffectTypes[effect_index], format, &candidate);
    if (error != ESP_OK) {
        s_requested_effect.store(state->effect_index);
        ESP_LOGE(kTag, "Could not create %s: %s; previous effect is unchanged",
                 kEffectNames[effect_index], esp_err_to_name(error));
        return;
    }
    state->effect = std::move(candidate);
    state->effect_index = effect_index;
    s_requested_effect.store(effect_index);
    ESP_LOGI(kTag, "Selected %s", kEffectNames[effect_index]);
}

void list_parameters(const PlaybackState& state) {
    if (!state.effect) {
        std::printf("FX_PARAM_COUNT effect=dry count=0\n");
        return;
    }
    std::printf("FX_PARAM_COUNT effect=%s count=%u\n",
                kEffectNames[state.effect_index],
                static_cast<unsigned>(state.effect->parameter_count()));
    for (size_t index = 0; index < state.effect->parameter_count(); ++index) {
        const audio_fx::ParameterInfo& info = state.effect->parameters()[index];
        float current_value = 0.0f;
        const esp_err_t error =
            state.effect->get_parameter(index, &current_value);
        if (error != ESP_OK) {
            ESP_LOGE(kTag, "Could not read parameter %u: %s",
                     static_cast<unsigned>(index), esp_err_to_name(error));
            continue;
        }
        std::printf(
            "FX_PARAM index=%u name=%s current=%.6f min=%.6f max=%.6f "
            "default=%.6f\n",
            static_cast<unsigned>(index), info.name, current_value,
            info.minimum, info.maximum, info.default_value);
    }
}

void apply_command(PlaybackState* state, const AudioCommand& command) {
    switch (command.type) {
        case CommandType::play: {
            if (command.track_index >= s_track_count) {
                ESP_LOGE(kTag, "Requested track index is out of range");
                break;
            }
            if (state->mode == AudioMode::microphone) {
                stop_stream(state, true);
            } else if (state->mode == AudioMode::wav) {
                report_stream_diagnostics(state, "replaced", true);
                close_stream(state);
            }
            char path[64]{};
            const Track& track = s_tracks[command.track_index];
            const int path_length = std::snprintf(
                path, sizeof(path), "/audio%u/%s",
                static_cast<unsigned>(track.filesystem_index),
                track.file_name);
            if (path_length < 0 ||
                static_cast<size_t>(path_length) >= sizeof(path)) {
                ESP_LOGE(kTag, "Track path is too long");
                break;
            }
            esp_err_t error = state->reader.open(path);
            if (error != ESP_OK) {
                ESP_LOGE(kTag, "Could not open %s: %s", path,
                         esp_err_to_name(error));
                break;
            }
            error = waveshare_audio::start_output();
            if (error != ESP_OK) {
                state->reader.close();
                ESP_LOGE(kTag, "Could not start speaker output: %s",
                         esp_err_to_name(error));
                break;
            }
            reset_stream_diagnostics(state);
            state->track_index = command.track_index;
            state->mode = AudioMode::wav;
            s_current_track.store(command.track_index);
            s_audio_mode.store(AudioMode::wav);
            ESP_LOGI(kTag, "Playing track %u: %s with %s",
                     static_cast<unsigned>(command.track_index + 1),
                     track.display_name,
                     kEffectNames[state->effect_index]);
            break;
        }
        case CommandType::stop:
            stop_stream(state, true);
            ESP_LOGI(kTag, "Audio stopped");
            break;
        case CommandType::start_live: {
            if (state->mode == AudioMode::microphone) {
                ESP_LOGI(kTag, "Live microphone stream is already running");
                break;
            }
            if (state->mode == AudioMode::wav) {
                stop_stream(state, true);
            } else {
                const esp_err_t stop_error = waveshare_audio::stop_output();
                if (stop_error != ESP_OK) {
                    ESP_LOGE(kTag, "Could not prepare codec output: %s",
                             esp_err_to_name(stop_error));
                    break;
                }
            }
            reset_stream_diagnostics(state);
            const esp_err_t error = waveshare_audio::start();
            if (error != ESP_OK) {
                ESP_LOGE(kTag, "Could not start live audio: %s",
                         esp_err_to_name(error));
                break;
            }
            state->microphone = static_cast<waveshare_audio::MicrophoneInput>(
                s_requested_microphone.load());
            state->mode = AudioMode::microphone;
            s_audio_mode.store(AudioMode::microphone);
            ESP_LOGI(kTag, "Live processing started at %u Hz (%s microphone)",
                     static_cast<unsigned>(kSampleRateHz),
                     state->microphone == waveshare_audio::MicrophoneInput::codec
                         ? "ES8311 codec"
                         : "INMP441");
            break;
        }
        case CommandType::select_microphone: {
            if (state->mode != AudioMode::stopped) {
                ESP_LOGE(kTag, "Stop audio before changing microphone input");
                break;
            }
            const esp_err_t stop_error = waveshare_audio::stop_output();
            if (stop_error != ESP_OK) {
                ESP_LOGE(kTag, "Could not stop speaker output: %s",
                         esp_err_to_name(stop_error));
                break;
            }
            const auto microphone =
                static_cast<waveshare_audio::MicrophoneInput>(
                    command.effect_index);
            const esp_err_t error =
                waveshare_audio::set_microphone_input(microphone);
            if (error != ESP_OK) {
                ESP_LOGE(kTag, "Could not select microphone: %s",
                         esp_err_to_name(error));
            } else {
                state->microphone = microphone;
                s_requested_microphone.store(command.effect_index);
                ESP_LOGI(kTag, "Selected %s microphone",
                         microphone == waveshare_audio::MicrophoneInput::codec
                             ? "ES8311 codec"
                             : "INMP441");
            }
            break;
        }
        case CommandType::set_microphone_gain: {
            if (state->microphone != waveshare_audio::MicrophoneInput::codec) {
                ESP_LOGE(kTag, "Select the codec microphone before setting gain");
                break;
            }
            const esp_err_t error =
                waveshare_audio::set_microphone_gain(command.value);
            if (error != ESP_OK) {
                ESP_LOGE(kTag, "Microphone gain update failed: %s",
                         esp_err_to_name(error));
            } else {
                ESP_LOGI(kTag, "Microphone gain set to %.1f dB",
                         command.value);
                s_requested_microphone_gain_db.store(command.value);
            }
            break;
        }
        case CommandType::select_effect:
            select_effect(state, command.effect_index);
            break;
        case CommandType::list_parameters:
            if (command.effect_index != state->effect_index) {
                ESP_LOGW(kTag, "Parameter query skipped after effect selection changed");
                break;
            }
            list_parameters(*state);
            break;
        case CommandType::get_bypass:
            if (!state->effect ||
                command.effect_index != state->effect_index) {
                std::printf("FX_BYPASS effect=dry bypassed=no\n");
            } else {
                std::printf("FX_BYPASS effect=%s bypassed=%s\n",
                            kEffectNames[state->effect_index],
                            state->effect->is_enabled() ? "no" : "yes");
            }
            break;
        case CommandType::set_parameter:
            if (!state->effect ||
                command.effect_index != state->effect_index) {
                ESP_LOGE(kTag, "Select a non-dry effect before setting parameters");
                break;
            }
            if (command.parameter_index >= state->effect->parameter_count()) {
                ESP_LOGE(kTag, "Parameter index %u is out of range for %s",
                         static_cast<unsigned>(command.parameter_index),
                         state->effect->name());
                break;
            }
            {
                const esp_err_t error = state->effect->set_parameter(
                    command.parameter_index, command.value);
                if (error != ESP_OK) {
                    ESP_LOGE(kTag, "%s parameter update failed: %s",
                             state->effect->name(), esp_err_to_name(error));
                } else {
                    ESP_LOGI(kTag, "%s parameter %u set to %.6f",
                             state->effect->name(),
                             static_cast<unsigned>(command.parameter_index),
                             command.value);
                }
            }
            break;
        case CommandType::set_bypass:
            if (!state->effect ||
                command.effect_index != state->effect_index) {
                ESP_LOGE(kTag, "Select a non-dry effect before bypass control");
                break;
            }
            state->effect->set_enabled(command.value == 0.0f);
            ESP_LOGI(kTag, "%s %s", state->effect->name(),
                     command.value == 0.0f ? "enabled" : "bypassed");
            break;
        case CommandType::set_volume: {
            const esp_err_t error =
                waveshare_audio::set_speaker_volume(command.value);
            if (error != ESP_OK) {
                ESP_LOGE(kTag, "Volume update failed: %s",
                         esp_err_to_name(error));
            } else {
                ESP_LOGI(kTag, "Speaker volume set to %.1f dB",
                         command.value);
                s_requested_volume_db.store(command.value);
            }
            break;
        }
    }
    if (command.reply_task) {
        xTaskNotifyGive(command.reply_task);
    }
}

esp_err_t read_next_block(PlaybackState* state, int16_t* live_input,
                          float* input, size_t* frames_read) {
    *frames_read = 0;
    if (state->mode == AudioMode::wav) {
        return state->reader.read_stereo(input, kBlockFrames, frames_read);
    }

    size_t bytes_read = 0;
    const size_t requested_bytes =
        kBlockFrames * kStereoChannels * sizeof(int16_t);
    const esp_err_t error = waveshare_audio::read(
        live_input, requested_bytes, &bytes_read, kReadTimeoutMs);
    if (error == ESP_ERR_TIMEOUT && bytes_read == 0) {
        return ESP_ERR_TIMEOUT;
    }
    if (error != ESP_OK && error != ESP_ERR_TIMEOUT) {
        return error;
    }
    constexpr size_t kBytesPerFrame = kStereoChannels * sizeof(int16_t);
    if (bytes_read % kBytesPerFrame != 0) {
        ESP_LOGW(kTag, "Discarding unaligned I2S input block (%u bytes)",
                 static_cast<unsigned>(bytes_read));
        return ESP_ERR_INVALID_SIZE;
    }
    *frames_read = bytes_read / kBytesPerFrame;
    if (*frames_read == 0 || *frames_read > kBlockFrames) {
        ESP_LOGW(kTag, "Discarding malformed I2S input block (%u bytes)",
                 static_cast<unsigned>(bytes_read));
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t frame = 0; frame < *frames_read; ++frame) {
        const float sample =
            static_cast<float>(live_input[frame * kStereoChannels]) / 32768.0f;
        input[frame * kStereoChannels] = sample;
        input[frame * kStereoChannels + 1] = sample;
    }
    return ESP_OK;
}

void audio_task(void*) {
    PlaybackState state;
    std::array<int16_t, kBlockFrames * kStereoChannels> live_input{};
    std::array<int16_t, kBlockFrames * kStereoChannels> output{};
    std::array<float, kBlockFrames * kStereoChannels> input{};
    std::array<float, kBlockFrames * kStereoChannels> processed{};

    for (;;) {
        AudioCommand command{};
        if (state.mode == AudioMode::stopped) {
            if (xQueueReceive(s_commands, &command, portMAX_DELAY) == pdTRUE) {
                apply_command(&state, command);
            }
            continue;
        }
        while (xQueueReceive(s_commands, &command, 0) == pdTRUE) {
            apply_command(&state, command);
        }
        if (state.mode == AudioMode::stopped) {
            continue;
        }

        size_t frames_read = 0;
        const esp_err_t read_error = read_next_block(
            &state, live_input.data(), input.data(), &frames_read);
        if (read_error == ESP_ERR_TIMEOUT && frames_read == 0) {
            continue;
        }
        if (read_error != ESP_OK) {
            ESP_LOGE(kTag, "Audio input failed: %s",
                     esp_err_to_name(read_error));
            report_stream_diagnostics(&state, "read_error", false);
            stop_stream(&state, false);
            continue;
        }
        if (state.mode == AudioMode::wav && frames_read == 0) {
            report_stream_diagnostics(&state, "eof", true);
            close_stream(&state);
            continue;
        }

        state.diagnostics.frames_read += frames_read;
        const size_t sample_count = frames_read * kStereoChannels;
        for (size_t sample_index = 0; sample_index < sample_count;
             ++sample_index) {
            if (!std::isfinite(input[sample_index])) {
                ++state.diagnostics.invalid_samples;
                input[sample_index] = 0.0f;
            }
            state.diagnostics.source.add(input[sample_index]);
        }

        esp_err_t process_error = ESP_OK;
        if (state.effect) {
            process_error = state.effect->process(
                input.data(), processed.data(), frames_read);
        } else {
            std::copy_n(input.data(), sample_count, processed.data());
        }
        if (process_error != ESP_OK) {
            ESP_LOGE(kTag, "Effect processing failed: %s",
                     esp_err_to_name(process_error));
            report_stream_diagnostics(&state, "dsp_error", false);
            stop_stream(&state, false);
            continue;
        }

        for (size_t sample_index = 0; sample_index < sample_count;
             ++sample_index) {
            float sample = processed[sample_index];
            if (!std::isfinite(sample)) {
                ++state.diagnostics.invalid_samples;
                sample = 0.0f;
            }
            if (sample < -1.0f || sample > 1.0f) {
                ++state.diagnostics.clipped_samples;
            }
            state.diagnostics.processed.add(sample);
            sample = std::clamp(sample, -1.0f, 1.0f);
            output[sample_index] =
                static_cast<int16_t>(sample * 32767.0f);
        }

        const size_t output_bytes =
            sample_count * sizeof(int16_t);
        if (!state.diagnostics.has_dma_baseline) {
            waveshare_audio::OutputDiagnostics before_write{};
            waveshare_audio::get_output_diagnostics(&before_write);
            state.diagnostics.dma_completions_at_start =
                before_write.dma_completions;
            state.diagnostics.dma_non_silent_at_start =
                before_write.dma_non_silent_buffers;
            state.diagnostics.dma_silent_at_start =
                before_write.dma_silent_buffers;
            state.diagnostics.dma_bytes_at_start = before_write.dma_bytes;
            state.diagnostics.has_dma_baseline = true;
        }

        size_t bytes_written = 0;
        const esp_err_t write_error = waveshare_audio::write(
            output.data(), output_bytes, &bytes_written, kWriteTimeoutMs);
        state.diagnostics.bytes_written += bytes_written;
        if (write_error != ESP_OK || bytes_written != output_bytes) {
            ++state.diagnostics.write_errors;
            state.diagnostics.last_write_error = write_error == ESP_OK
                ? ESP_ERR_INVALID_SIZE
                : write_error;
            ESP_LOGE(kTag, "I2S output failed: %s (wrote %u of %u bytes)",
                     esp_err_to_name(write_error),
                     static_cast<unsigned>(bytes_written),
                     static_cast<unsigned>(output_bytes));
            report_stream_diagnostics(&state, "i2s_error", false);
            stop_stream(&state, false);
        }
    }
}

}  // namespace

extern "C" void app_main(void) {
    esp_vfs_spiffs_conf_t storage_config{};
    storage_config.base_path = "/audio0";
    storage_config.partition_label = "audio0";
    storage_config.max_files = 4;
    storage_config.format_if_mount_failed = false;
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&storage_config));

    storage_config.base_path = "/audio1";
    storage_config.partition_label = "audio1";
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&storage_config));
    for (const char* label : {"audio0", "audio1"}) {
        size_t total_bytes = 0;
        size_t used_bytes = 0;
        ESP_ERROR_CHECK(esp_spiffs_info(label, &total_bytes, &used_bytes));
        ESP_LOGI(kTag, "%s SPIFFS: %u of %u bytes used", label,
                 static_cast<unsigned>(used_bytes),
                 static_cast<unsigned>(total_bytes));
    }
    ESP_ERROR_CHECK(load_tracks());
    ESP_ERROR_CHECK(waveshare_audio::initialize());

    s_commands = xQueueCreate(kQueueLength, sizeof(AudioCommand));
    if (!s_commands) {
        ESP_LOGE(kTag, "Failed to create audio command queue");
        abort();
    }
    if (xTaskCreatePinnedToCore(audio_task, "audio_fx_stream", 8192, nullptr,
                                tskIDLE_PRIORITY + 5, nullptr, 1) != pdPASS) {
        ESP_LOGE(kTag, "Failed to create audio processing task");
        abort();
    }
    ESP_ERROR_CHECK(initialize_console());
    ESP_LOGI(kTag,
             "Unified console ready; firmware identity is %s. Try 'help'.",
             kFirmwareIdentity);
}
