#include "audio_board.hpp"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/i2s_types.h"
#include "esp_attr.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include <atomic>
#include <cstdint>

namespace waveshare_audio {
namespace {

constexpr char kTag[] = "audio_board";
constexpr uint32_t kSampleRateHz = 48000;
constexpr int kI2cPort = I2C_NUM_0;
constexpr int kI2sPort = I2S_NUM_0;
constexpr gpio_num_t kI2cScl = GPIO_NUM_8;
constexpr gpio_num_t kI2cSda = GPIO_NUM_7;
constexpr gpio_num_t kI2sMclk = GPIO_NUM_13;
constexpr gpio_num_t kI2sBclk = GPIO_NUM_12;
constexpr gpio_num_t kI2sWs = GPIO_NUM_10;
constexpr gpio_num_t kI2sDin = GPIO_NUM_11;
constexpr gpio_num_t kI2sDout = GPIO_NUM_9;
constexpr gpio_num_t kAmplifierEnable = GPIO_NUM_53;
constexpr gpio_num_t kInmp441Bclk = GPIO_NUM_20;
constexpr gpio_num_t kInmp441Ws = GPIO_NUM_21;
constexpr gpio_num_t kInmp441Din = GPIO_NUM_22;
constexpr float kInitialSpeakerVolumeDb = -30.0f;
constexpr float kInitialMicrophoneGainDb = 0.0f;

i2c_master_bus_handle_t s_i2c_bus = nullptr;
i2s_chan_handle_t s_i2s_rx = nullptr;
i2s_chan_handle_t s_i2s_tx = nullptr;
i2s_chan_handle_t s_inmp441_rx = nullptr;
const audio_codec_if_t* s_codec = nullptr;
const audio_codec_ctrl_if_t* s_codec_control = nullptr;
const audio_codec_data_if_t* s_codec_data = nullptr;
const audio_codec_gpio_if_t* s_codec_gpio = nullptr;
bool s_initialized = false;
bool s_capture_started = false;
bool s_output_started = false;
MicrophoneInput s_microphone_input = MicrophoneInput::inmp441;
std::atomic<uint32_t> s_dma_completions{0};
std::atomic<uint32_t> s_dma_bytes{0};
std::atomic<uint32_t> s_dma_non_silent_buffers{0};
std::atomic<uint32_t> s_dma_silent_buffers{0};
std::atomic<uint32_t> s_dma_peak_sample{0};
std::atomic<uint32_t> s_dma_buffer_bytes{0};

static_assert(std::atomic<uint32_t>::is_always_lock_free);

bool IRAM_ATTR on_output_sent(i2s_chan_handle_t,
                              i2s_event_data_t* event,
                              void*) noexcept {
    if (!event || !event->dma_buf) {
        return false;
    }

    const auto* samples = static_cast<const int16_t*>(event->dma_buf);
    const size_t sample_count = event->size / sizeof(int16_t);
    uint32_t peak = 0;
    for (size_t index = 0; index < sample_count; ++index) {
        const int32_t sample = samples[index];
        const uint32_t magnitude = static_cast<uint32_t>(
            sample < 0 ? -sample : sample);
        if (magnitude > peak) {
            peak = magnitude;
        }
    }

    s_dma_completions.fetch_add(1, std::memory_order_relaxed);
    s_dma_bytes.fetch_add(static_cast<uint32_t>(event->size),
                          std::memory_order_relaxed);
    s_dma_buffer_bytes.store(static_cast<uint32_t>(event->size),
                             std::memory_order_relaxed);
    if (peak == 0) {
        s_dma_silent_buffers.fetch_add(1, std::memory_order_relaxed);
    } else {
        s_dma_non_silent_buffers.fetch_add(1, std::memory_order_relaxed);
        uint32_t previous_peak =
            s_dma_peak_sample.load(std::memory_order_relaxed);
        while (peak > previous_peak &&
               !s_dma_peak_sample.compare_exchange_weak(
                   previous_peak, peak, std::memory_order_relaxed)) {
        }
    }
    return false;
}

esp_err_t initialize_i2c() noexcept {
    i2c_master_bus_config_t config{};
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.i2c_port = kI2cPort;
    config.scl_io_num = kI2cScl;
    config.sda_io_num = kI2cSda;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&config, &s_i2c_bus);
}

esp_err_t initialize_i2s() noexcept {
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;

    esp_err_t error = i2s_new_channel(
        &channel_config, &s_i2s_tx, &s_i2s_rx);
    if (error != ESP_OK) {
        return error;
    }

    i2s_std_config_t standard_config{};
    standard_config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRateHz);
    standard_config.clk_cfg.clk_src = I2S_CLK_SRC_APLL;
    standard_config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    standard_config.gpio_cfg.mclk = kI2sMclk;
    standard_config.gpio_cfg.bclk = kI2sBclk;
    standard_config.gpio_cfg.ws = kI2sWs;
    standard_config.gpio_cfg.dout = kI2sDout;
    standard_config.gpio_cfg.din = kI2sDin;
    standard_config.gpio_cfg.invert_flags.mclk_inv = false;
    standard_config.gpio_cfg.invert_flags.bclk_inv = false;
    standard_config.gpio_cfg.invert_flags.ws_inv = false;

    error = i2s_channel_init_std_mode(s_i2s_rx, &standard_config);
    if (error != ESP_OK) {
        return error;
    }
    error = i2s_channel_init_std_mode(s_i2s_tx, &standard_config);
    if (error != ESP_OK) {
        return error;
    }
    i2s_event_callbacks_t tx_callbacks{};
    tx_callbacks.on_sent = on_output_sent;
    error = i2s_channel_register_event_callback(s_i2s_tx, &tx_callbacks,
                                                nullptr);
    if (error != ESP_OK) {
        return error;
    }

    i2s_chan_config_t microphone_channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    microphone_channel_config.auto_clear = true;
    error = i2s_new_channel(&microphone_channel_config, nullptr,
                            &s_inmp441_rx);
    if (error != ESP_OK) {
        return error;
    }

    i2s_std_config_t microphone_config{};
    microphone_config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRateHz);
    microphone_config.clk_cfg.clk_src = I2S_CLK_SRC_APLL;
    microphone_config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    microphone_config.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    microphone_config.gpio_cfg.bclk = kInmp441Bclk;
    microphone_config.gpio_cfg.ws = kInmp441Ws;
    microphone_config.gpio_cfg.dout = I2S_GPIO_UNUSED;
    microphone_config.gpio_cfg.din = kInmp441Din;
    microphone_config.gpio_cfg.invert_flags.mclk_inv = false;
    microphone_config.gpio_cfg.invert_flags.bclk_inv = false;
    microphone_config.gpio_cfg.invert_flags.ws_inv = false;
    return i2s_channel_init_std_mode(s_inmp441_rx, &microphone_config);
}

esp_err_t initialize_codec() noexcept {
    audio_codec_i2c_cfg_t control_config{};
    control_config.port = kI2cPort;
    control_config.addr = ES8311_CODEC_DEFAULT_ADDR;
    control_config.bus_handle = s_i2c_bus;
    s_codec_control = audio_codec_new_i2c_ctrl(&control_config);
    if (!s_codec_control) {
        return ESP_ERR_NO_MEM;
    }

    audio_codec_i2s_cfg_t data_config{};
    data_config.port = kI2sPort;
    data_config.rx_handle = s_i2s_rx;
    data_config.tx_handle = s_i2s_tx;
    s_codec_data = audio_codec_new_i2s_data(&data_config);
    if (!s_codec_data) {
        return ESP_ERR_NO_MEM;
    }

    s_codec_gpio = audio_codec_new_gpio();
    if (!s_codec_gpio) {
        return ESP_ERR_NO_MEM;
    }

    es8311_codec_cfg_t codec_config{};
    codec_config.ctrl_if = s_codec_control;
    codec_config.gpio_if = s_codec_gpio;
    codec_config.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    codec_config.pa_pin = kAmplifierEnable;
    codec_config.pa_reverted = false;
    codec_config.master_mode = false;
    codec_config.use_mclk = true;
    codec_config.digital_mic = false;
    codec_config.invert_mclk = false;
    codec_config.invert_sclk = false;
    codec_config.hw_gain.pa_voltage = 5.0;
    codec_config.hw_gain.codec_dac_voltage = 3.3;
    s_codec = es8311_codec_new(&codec_config);
    if (!s_codec) {
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_sample_info_t sample_info{};
    sample_info.bits_per_sample = 16;
    sample_info.channel = 2;
    sample_info.sample_rate = kSampleRateHz;
    const esp_err_t error = s_codec->set_fs(s_codec, &sample_info);
    if (error != ESP_OK) {
        return error;
    }

    return s_codec->set_mic_gain(s_codec, kInitialMicrophoneGainDb);
}

void log_cleanup_error(const char* operation, esp_err_t error) noexcept {
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "%s cleanup failed: %s", operation,
                 esp_err_to_name(error));
    }
}

}  // namespace

esp_err_t initialize() noexcept {
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = initialize_i2c();
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "I2C initialization failed: %s", esp_err_to_name(error));
        return error;
    }
    error = initialize_i2s();
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "I2S initialization failed: %s", esp_err_to_name(error));
        return error;
    }
    error = initialize_codec();
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "ES8311 initialization failed: %s",
                 esp_err_to_name(error));
        return error;
    }
    error = set_speaker_volume(kInitialSpeakerVolumeDb);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "Could not set safe startup volume: %s",
                 esp_err_to_name(error));
        return error;
    }

    s_initialized = true;
    ESP_LOGI(kTag, "ES8311 ready at %u Hz; startup volume is %.1f dB",
             static_cast<unsigned>(kSampleRateHz),
             kInitialSpeakerVolumeDb);
    return ESP_OK;
}

esp_err_t start() noexcept {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_capture_started && s_output_started) {
        return ESP_OK;
    }
    if (s_capture_started || s_output_started) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t error = i2s_channel_enable(s_i2s_rx);
    if (error != ESP_OK) {
        return error;
    }
    error = i2s_channel_enable(s_inmp441_rx);
    if (error != ESP_OK) {
        log_cleanup_error("codec RX channel disable",
                          i2s_channel_disable(s_i2s_rx));
        return error;
    }
    error = i2s_channel_enable(s_i2s_tx);
    if (error != ESP_OK) {
        log_cleanup_error("INMP441 RX channel disable",
                          i2s_channel_disable(s_inmp441_rx));
        log_cleanup_error("codec RX channel disable",
                          i2s_channel_disable(s_i2s_rx));
        return error;
    }
    error = s_codec->enable(s_codec, true);
    if (error != ESP_OK) {
        log_cleanup_error("TX channel disable",
                          i2s_channel_disable(s_i2s_tx));
        log_cleanup_error("INMP441 RX channel disable",
                          i2s_channel_disable(s_inmp441_rx));
        log_cleanup_error("codec RX channel disable",
                          i2s_channel_disable(s_i2s_rx));
        return error;
    }

    s_capture_started = true;
    s_output_started = true;
    return ESP_OK;
}

esp_err_t start_output() noexcept {
    if (!s_initialized || s_capture_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_output_started) {
        return ESP_OK;
    }

    esp_err_t error = i2s_channel_enable(s_i2s_rx);
    if (error != ESP_OK) {
        return error;
    }
    error = i2s_channel_enable(s_i2s_tx);
    if (error != ESP_OK) {
        log_cleanup_error("codec RX channel disable",
                          i2s_channel_disable(s_i2s_rx));
        return error;
    }
    error = s_codec->enable(s_codec, true);
    if (error != ESP_OK) {
        log_cleanup_error("TX channel disable", i2s_channel_disable(s_i2s_tx));
        log_cleanup_error("codec RX channel disable",
                          i2s_channel_disable(s_i2s_rx));
        return error;
    }
    s_output_started = true;
    return ESP_OK;
}

esp_err_t stop() noexcept {
    if (!s_capture_started && !s_output_started) {
        return ESP_OK;
    }

    esp_err_t first_error = ESP_OK;
    if (s_output_started) {
        first_error = s_codec->enable(s_codec, false);
        const esp_err_t error = i2s_channel_disable(s_i2s_tx);
        if (first_error == ESP_OK) {
            first_error = error;
        }
        s_output_started = false;
    }
    if (s_capture_started) {
        const esp_err_t inmp441_error = i2s_channel_disable(s_inmp441_rx);
        if (first_error == ESP_OK) {
            first_error = inmp441_error;
        }
        const esp_err_t codec_rx_error = i2s_channel_disable(s_i2s_rx);
        if (first_error == ESP_OK) {
            first_error = codec_rx_error;
        }
        s_capture_started = false;
    }
    return first_error;
}

esp_err_t stop_output() noexcept {
    if (s_capture_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_output_started) {
        return ESP_OK;
    }

    const esp_err_t codec_error = s_codec->enable(s_codec, false);
    const esp_err_t tx_error = i2s_channel_disable(s_i2s_tx);
    const esp_err_t rx_error = i2s_channel_disable(s_i2s_rx);
    s_output_started = false;
    if (codec_error != ESP_OK) {
        return codec_error;
    }
    return tx_error != ESP_OK ? tx_error : rx_error;
}

esp_err_t set_microphone_input(MicrophoneInput input) noexcept {
    if (!s_initialized || s_capture_started || s_output_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (input != MicrophoneInput::codec &&
        input != MicrophoneInput::inmp441) {
        return ESP_ERR_INVALID_ARG;
    }
    s_microphone_input = input;
    return ESP_OK;
}

esp_err_t read(int16_t* interleaved_stereo, size_t buffer_bytes,
               size_t* bytes_read, uint32_t timeout_ms) noexcept {
    if (!s_capture_started) {
        return ESP_ERR_INVALID_STATE;
    }
    const i2s_chan_handle_t microphone_channel =
        s_microphone_input == MicrophoneInput::codec ? s_i2s_rx : s_inmp441_rx;
    return i2s_channel_read(microphone_channel, interleaved_stereo,
                            buffer_bytes, bytes_read,
                            pdMS_TO_TICKS(timeout_ms));
}

esp_err_t write(const int16_t* interleaved_stereo, size_t buffer_bytes,
                size_t* bytes_written, uint32_t timeout_ms) noexcept {
    if (!s_output_started) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2s_channel_write(s_i2s_tx, interleaved_stereo, buffer_bytes,
                             bytes_written, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t set_speaker_volume(float volume_db) noexcept {
    if (!s_codec || volume_db < -60.0f || volume_db > 0.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    return s_codec->set_vol(s_codec, volume_db);
}

esp_err_t set_microphone_gain(float gain_db) noexcept {
    if (!s_codec || s_microphone_input != MicrophoneInput::codec ||
        gain_db < 0.0f || gain_db > 42.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    return s_codec->set_mic_gain(s_codec, gain_db);
}

void reset_output_diagnostics() noexcept {
    s_dma_completions.store(0, std::memory_order_relaxed);
    s_dma_bytes.store(0, std::memory_order_relaxed);
    s_dma_non_silent_buffers.store(0, std::memory_order_relaxed);
    s_dma_silent_buffers.store(0, std::memory_order_relaxed);
    s_dma_peak_sample.store(0, std::memory_order_relaxed);
    s_dma_buffer_bytes.store(0, std::memory_order_relaxed);
}

void get_output_diagnostics(OutputDiagnostics* diagnostics) noexcept {
    if (!diagnostics) {
        return;
    }
    diagnostics->dma_completions =
        s_dma_completions.load(std::memory_order_relaxed);
    diagnostics->dma_bytes = s_dma_bytes.load(std::memory_order_relaxed);
    diagnostics->dma_non_silent_buffers =
        s_dma_non_silent_buffers.load(std::memory_order_relaxed);
    diagnostics->dma_silent_buffers =
        s_dma_silent_buffers.load(std::memory_order_relaxed);
    diagnostics->dma_peak_sample =
        s_dma_peak_sample.load(std::memory_order_relaxed);
    diagnostics->dma_buffer_bytes =
        s_dma_buffer_bytes.load(std::memory_order_relaxed);
}

}  // namespace waveshare_audio
