#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>

#include "audio_fx/audio_fx.hpp"
#include "esp_timer.h"
#include "unity.h"

namespace {

constexpr size_t kTestFrames = 2048;
constexpr size_t kStereoRampFrames = 240;
constexpr size_t kStereoChannels = 2;
constexpr size_t kBenchmarkFramesPerBlock = 240;
constexpr size_t kBenchmarkBlockCount = 200;
float s_input[kTestFrames];
float s_output[kTestFrames];
std::array<float, kStereoRampFrames * kStereoChannels> s_stereo_input{};
std::array<float, kStereoRampFrames * kStereoChannels> s_stereo_output{};
std::array<float, kBenchmarkFramesPerBlock * kStereoChannels>
    s_benchmark_input{};
std::array<float, kBenchmarkFramesPerBlock * kStereoChannels>
    s_benchmark_output{};

audio_fx::AudioFormat mono_format() {
    return {16000, 1};
}

}  // namespace

TEST_CASE("factory validates formats and preserves output on failure",
          "[audio_fx]") {
    std::unique_ptr<audio_fx::Effect> effect;
    TEST_ASSERT_EQUAL(ESP_OK, audio_fx::create_effect(
                                  audio_fx::EffectType::reverb, mono_format(),
                                  &effect));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      audio_fx::create_effect(
                          audio_fx::EffectType::reverb,
                          {0, 1}, &effect));
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        audio_fx::create_effect(
            audio_fx::EffectType::reverb,
            {7999, 1}, &effect));
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        audio_fx::create_effect(
            audio_fx::EffectType::reverb,
            {96001, 1}, &effect));
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        audio_fx::create_effect(
            audio_fx::EffectType::reverb,
            {16000, 3}, &effect));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      audio_fx::create_effect(
                          static_cast<audio_fx::EffectType>(255), mono_format(),
                          &effect));
    TEST_ASSERT_EQUAL_STRING("reverb", effect->name());

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      audio_fx::create_effect(audio_fx::EffectType::reverb,
                                              mono_format(), nullptr));
    TEST_ASSERT_EQUAL(
        ESP_OK,
        audio_fx::create_effect(
            audio_fx::EffectType::equalizer,
            {96000, 1}, &effect));
}

TEST_CASE("reverb validates parameters and produces finite impulse response",
          "[audio_fx]") {
    std::unique_ptr<audio_fx::Effect> effect;
    TEST_ASSERT_EQUAL(ESP_OK, audio_fx::create_effect(
                                  audio_fx::EffectType::reverb, mono_format(),
                                  &effect));
    TEST_ASSERT_EQUAL_STRING("reverb", effect->name());
    TEST_ASSERT_EQUAL(3, effect->parameter_count());
    TEST_ASSERT_GREATER_THAN(0, effect->memory_usage_bytes());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, effect->set_parameter(1, 1.1f));

    for (size_t i = 0; i < kTestFrames; ++i) {
        s_input[i] = 0.0f;
        s_output[i] = 0.0f;
    }
    s_input[0] = 0.5f;
    TEST_ASSERT_EQUAL(ESP_OK,
                      effect->process(s_input, s_output, kTestFrames));

    bool has_wet_output = false;
    for (float sample : s_output) {
        TEST_ASSERT_TRUE(std::isfinite(sample));
        has_wet_output = has_wet_output || std::fabs(sample) > 1.0e-7f;
    }
    TEST_ASSERT_TRUE(has_wet_output);
}

TEST_CASE("chorus handles stereo in-place blocks and parameter bounds",
          "[audio_fx]") {
    const audio_fx::AudioFormat stereo{
        48000,
        2,
    };
    std::unique_ptr<audio_fx::Effect> effect;
    TEST_ASSERT_EQUAL(ESP_OK, audio_fx::create_effect(
                                  audio_fx::EffectType::chorus, stereo, &effect));
    TEST_ASSERT_EQUAL_STRING("chorus", effect->name());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, effect->set_parameter(2, 11.0f));

    for (size_t i = 0; i < kTestFrames; ++i) {
        s_input[i] = (i == 0) ? 0.25f : 0.0f;
    }
    TEST_ASSERT_EQUAL(ESP_OK,
                      effect->process(s_input, s_input, kTestFrames / 2));
    for (size_t i = 0; i < kTestFrames; ++i) {
        TEST_ASSERT_TRUE(std::isfinite(s_input[i]));
    }
}

TEST_CASE("delay provides bounded fractional feedback in mono",
          "[audio_fx]") {
    std::unique_ptr<audio_fx::Effect> effect;
    TEST_ASSERT_EQUAL(ESP_OK, audio_fx::create_effect(
                                  audio_fx::EffectType::delay, {8000, 1},
                                  &effect));
    TEST_ASSERT_EQUAL_STRING("delay", effect->name());
    TEST_ASSERT_EQUAL(3, effect->parameter_count());
    TEST_ASSERT_GREATER_THAN(0, effect->memory_usage_bytes());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, effect->set_parameter(2, 0.96f));

    s_input[0] = 0.5f;
    for (size_t frame = 1; frame < kTestFrames; ++frame) {
        s_input[frame] = 0.0f;
    }
    TEST_ASSERT_EQUAL(ESP_OK,
                      effect->process(s_input, s_output, kTestFrames));
    for (float sample : s_output) {
        TEST_ASSERT_TRUE(std::isfinite(sample));
    }
    TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 0.175f, s_output[2000]);

    for (size_t frame = 0; frame < kTestFrames; ++frame) {
        s_input[frame] = 0.0f;
    }
    TEST_ASSERT_EQUAL(ESP_OK,
                      effect->process(s_input, s_output, kTestFrames));
    TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 0.06125f, s_output[1952]);
}

TEST_CASE("flanger validates modulation controls and processes stereo",
          "[audio_fx]") {
    const audio_fx::AudioFormat stereo{16000, kStereoChannels};
    std::unique_ptr<audio_fx::Effect> effect;
    TEST_ASSERT_EQUAL(ESP_OK, audio_fx::create_effect(
                                  audio_fx::EffectType::flanger, stereo,
                                  &effect));
    TEST_ASSERT_EQUAL_STRING("flanger", effect->name());
    TEST_ASSERT_EQUAL(4, effect->parameter_count());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, effect->set_parameter(3, 1.0f));

    s_stereo_input.fill(0.0f);
    s_stereo_input[0] = 0.5f;
    s_stereo_input[1] = -0.5f;
    TEST_ASSERT_EQUAL(ESP_OK, effect->process(
                                  s_stereo_input.data(),
                                  s_stereo_output.data(),
                                  kStereoRampFrames));
    for (float sample : s_stereo_output) {
        TEST_ASSERT_TRUE(std::isfinite(sample));
    }
    bool has_wet_output = false;
    for (float sample : s_stereo_output) {
        has_wet_output = has_wet_output || std::fabs(sample) > 1.0e-4f;
    }
    TEST_ASSERT_TRUE(has_wet_output);
}

TEST_CASE("bitcrusher enforces integer settings and quantizes samples",
          "[audio_fx]") {
    std::unique_ptr<audio_fx::Effect> effect;
    TEST_ASSERT_EQUAL(ESP_OK, audio_fx::create_effect(
                                  audio_fx::EffectType::bitcrusher,
                                  mono_format(), &effect));
    TEST_ASSERT_EQUAL_STRING("bitcrusher", effect->name());
    TEST_ASSERT_EQUAL(3, effect->parameter_count());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, effect->set_parameter(1, 2.5f));
    TEST_ASSERT_EQUAL(ESP_OK, effect->set_parameter(1, 1.0f));
    TEST_ASSERT_EQUAL(ESP_OK, effect->set_parameter(2, 1.0f));

    s_input[0] = 0.25f;
    TEST_ASSERT_EQUAL(ESP_OK, effect->process(s_input, s_output, 1));
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 1.0f, s_output[0]);
}

TEST_CASE("compressor reduces level with linked envelope control",
          "[audio_fx]") {
    const audio_fx::AudioFormat stereo{16000, kStereoChannels};
    std::unique_ptr<audio_fx::Effect> effect;
    TEST_ASSERT_EQUAL(ESP_OK, audio_fx::create_effect(
                                  audio_fx::EffectType::compressor,
                                  stereo, &effect));
    TEST_ASSERT_EQUAL_STRING("compressor", effect->name());
    TEST_ASSERT_EQUAL(7, effect->parameter_count());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, effect->set_parameter(1, 0.5f));

    for (size_t frame = 0; frame < kStereoRampFrames; ++frame) {
        s_stereo_input[frame * kStereoChannels] = 0.5f;
        s_stereo_input[frame * kStereoChannels + 1] = 0.1f;
    }
    TEST_ASSERT_EQUAL(
        ESP_OK, effect->process(s_stereo_input.data(), s_stereo_output.data(),
                                kStereoRampFrames));
    for (float sample : s_stereo_output) {
        TEST_ASSERT_TRUE(std::isfinite(sample));
    }
    const size_t last_frame = (kStereoRampFrames - 1) * kStereoChannels;
    TEST_ASSERT_TRUE(s_stereo_output[last_frame] < 0.3f);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, 0.2f,
                             s_stereo_output[last_frame + 1] /
                                 s_stereo_output[last_frame]);
}

TEST_CASE("look-ahead limiter respects its sample-peak ceiling",
          "[audio_fx]") {
    const audio_fx::AudioFormat stereo{16000, kStereoChannels};
    std::unique_ptr<audio_fx::Effect> effect;
    TEST_ASSERT_EQUAL(ESP_OK, audio_fx::create_effect(
                                  audio_fx::EffectType::limiter,
                                  stereo, &effect));
    TEST_ASSERT_EQUAL_STRING("limiter", effect->name());
    TEST_ASSERT_EQUAL(2, effect->parameter_count());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, effect->set_parameter(0, 1.0f));
    TEST_ASSERT_EQUAL(ESP_OK, effect->set_parameter(0, -6.0f));
    TEST_ASSERT_EQUAL(ESP_OK, effect->set_parameter(1, 10.0f));

    s_stereo_input.fill(0.0f);
    s_stereo_input[0] = 2.0f;
    s_stereo_input[1] = 0.2f;
    TEST_ASSERT_EQUAL(
        ESP_OK, effect->process(s_stereo_input.data(), s_stereo_output.data(),
                                kStereoRampFrames));
    for (float sample : s_stereo_output) {
        TEST_ASSERT_TRUE(std::isfinite(sample));
        TEST_ASSERT_TRUE(std::fabs(sample) <= 0.5013f);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.501187f, s_stereo_output[160]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f,
                             s_stereo_output[161] /
                                 s_stereo_output[160]);
}

TEST_CASE("equalizer applies its center-frequency gain and validates Nyquist",
          "[audio_fx]") {
    std::unique_ptr<audio_fx::Effect> effect;
    TEST_ASSERT_EQUAL(ESP_OK, audio_fx::create_effect(
                                  audio_fx::EffectType::equalizer, mono_format(),
                                  &effect));
    TEST_ASSERT_EQUAL_STRING("equalizer", effect->name());
    TEST_ASSERT_EQUAL(15, effect->parameter_count());
    TEST_ASSERT_GREATER_THAN(0, effect->memory_usage_bytes());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, effect->set_parameter(12, 7200.1f));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, effect->set_parameter(7, 12.1f));

    constexpr float kPi = 3.14159265358979323846f;
    constexpr size_t kToneFrames = 2048;
    TEST_ASSERT_EQUAL(ESP_OK, effect->set_parameter(6, 1000.0f));
    TEST_ASSERT_EQUAL(ESP_OK, effect->set_parameter(7, 6.0f));
    TEST_ASSERT_EQUAL(ESP_OK, effect->set_parameter(8, 0.707f));
    for (size_t frame = 0; frame < kToneFrames; ++frame) {
        s_input[frame] =
            0.1f * std::sin(2.0f * kPi * 1000.0f *
                            static_cast<float>(frame) / 16000.0f);
    }
    TEST_ASSERT_EQUAL(ESP_OK,
                      effect->process(s_input, s_output, kToneFrames));

    float input_energy = 0.0f;
    float output_energy = 0.0f;
    for (size_t frame = kToneFrames / 2; frame < kToneFrames; ++frame) {
        input_energy += s_input[frame] * s_input[frame];
        output_energy += s_output[frame] * s_output[frame];
        TEST_ASSERT_TRUE(std::isfinite(s_output[frame]));
    }
    const float amplitude_ratio = std::sqrt(output_energy / input_energy);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.995f, amplitude_ratio);
}

TEST_CASE("equalizer advances coefficient ramps once per stereo frame",
          "[audio_fx]") {
    const audio_fx::AudioFormat stereo{
        48000,
        kStereoChannels,
    };
    std::unique_ptr<audio_fx::Effect> effect;
    TEST_ASSERT_EQUAL(ESP_OK, audio_fx::create_effect(
                                  audio_fx::EffectType::equalizer, stereo,
                                  &effect));
    TEST_ASSERT_EQUAL(ESP_OK, effect->set_parameter(7, 6.0f));

    s_stereo_input.fill(0.0f);
    s_stereo_output.fill(0.0f);
    s_stereo_input[0] = 1.0f;
    s_stereo_input[1] = 1.0f;
    TEST_ASSERT_EQUAL(ESP_OK, effect->process(s_stereo_input.data(),
                                              s_stereo_output.data(),
                                              kStereoRampFrames));
    for (size_t frame = 0; frame < kStereoRampFrames; ++frame) {
        TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, s_stereo_output[frame * kStereoChannels],
                                 s_stereo_output[frame * kStereoChannels + 1]);
    }
}

TEST_CASE("equalizer metadata reflects the configured sample-rate limit",
          "[audio_fx]") {
    constexpr uint32_t kSampleRateHz = 8000;
    constexpr float kNyquistLimitHz = kSampleRateHz * 0.45f;
    std::unique_ptr<audio_fx::Effect> effect;
    TEST_ASSERT_EQUAL(ESP_OK, audio_fx::create_effect(
                                  audio_fx::EffectType::equalizer,
                                  {kSampleRateHz, 1},
                                  &effect));

    const float maximum_frequency_hz = effect->parameters()[0].maximum;
    TEST_ASSERT_EQUAL_FLOAT(kNyquistLimitHz, maximum_frequency_hz);
    TEST_ASSERT_EQUAL(ESP_OK, effect->set_parameter(0, maximum_frequency_hz));
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        effect->set_parameter(
            0, std::nextafter(maximum_frequency_hz,
                              std::numeric_limits<float>::infinity())));
}

TEST_CASE("effects process one second of stereo audio in real time",
          "[audio_fx]") {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr uint32_t kSampleRateHz = 48000;
    constexpr int64_t kAudioDurationUs = 1000000;
    const audio_fx::AudioFormat stereo{
        kSampleRateHz,
        kStereoChannels,
    };
    const audio_fx::EffectType effect_types[] = {
        audio_fx::EffectType::reverb,
        audio_fx::EffectType::chorus,
        audio_fx::EffectType::equalizer,
        audio_fx::EffectType::delay,
        audio_fx::EffectType::flanger,
        audio_fx::EffectType::bitcrusher,
        audio_fx::EffectType::compressor,
        audio_fx::EffectType::limiter,
    };

    for (size_t frame = 0; frame < kBenchmarkFramesPerBlock; ++frame) {
        const float sample =
            0.1f * std::sin(2.0f * kPi * 440.0f *
                            static_cast<float>(frame) / kSampleRateHz);
        s_benchmark_input[frame * kStereoChannels] = sample;
        s_benchmark_input[frame * kStereoChannels + 1] = sample;
    }

    for (audio_fx::EffectType type : effect_types) {
        std::unique_ptr<audio_fx::Effect> effect;
        TEST_ASSERT_EQUAL(ESP_OK,
                          audio_fx::create_effect(type, stereo, &effect));

        const int64_t start_us = esp_timer_get_time();
        for (size_t block = 0; block < kBenchmarkBlockCount; ++block) {
            TEST_ASSERT_EQUAL(
                ESP_OK, effect->process(s_benchmark_input.data(),
                                        s_benchmark_output.data(),
                                        kBenchmarkFramesPerBlock));
        }
        const int64_t elapsed_us = esp_timer_get_time() - start_us;
        const float realtime_load_percent =
            static_cast<float>(elapsed_us) * 100.0f /
            static_cast<float>(kAudioDurationUs);
        std::printf("%s: %.2f%% of one P4 core for 48 kHz stereo\n",
                    effect->name(), realtime_load_percent);
        TEST_ASSERT_TRUE_MESSAGE(elapsed_us < kAudioDurationUs,
                                 effect->name());
    }
}

TEST_CASE("disabled effect ramps to dry signal", "[audio_fx]") {
    std::unique_ptr<audio_fx::Effect> effect;
    TEST_ASSERT_EQUAL(ESP_OK, audio_fx::create_effect(
                                  audio_fx::EffectType::chorus, mono_format(),
                                  &effect));
    effect->set_enabled(false);

    for (size_t i = 0; i < kTestFrames; ++i) {
        s_input[i] = 0.0f;
        s_output[i] = 0.0f;
    }
    TEST_ASSERT_EQUAL(ESP_OK, effect->process(s_input, s_output, 80));
    s_input[80] = 0.375f;
    TEST_ASSERT_EQUAL(ESP_OK, effect->process(&s_input[80], &s_output[80], 1));
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.375f, s_output[80]);
}

TEST_CASE("processing rejects invalid buffers and overflowing frame counts",
          "[audio_fx]") {
    std::unique_ptr<audio_fx::Effect> effect;
    TEST_ASSERT_EQUAL(ESP_OK, audio_fx::create_effect(
                                  audio_fx::EffectType::reverb, mono_format(),
                                  &effect));
    TEST_ASSERT_EQUAL(ESP_OK, effect->process(nullptr, nullptr, 0));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, effect->process(nullptr, s_output, 1));

    const audio_fx::AudioFormat stereo{
        48000,
        kStereoChannels,
    };
    TEST_ASSERT_EQUAL(ESP_OK, audio_fx::create_effect(
                                  audio_fx::EffectType::equalizer, stereo,
                                  &effect));
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        effect->process(s_input, s_output,
                        std::numeric_limits<size_t>::max() / kStereoChannels +
                            1));
}

extern "C" void app_main(void) {
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
}
