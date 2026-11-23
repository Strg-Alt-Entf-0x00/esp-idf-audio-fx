#pragma once

#include "audio_fx/effect.hpp"

namespace audio_fx {

class Limiter final : public Effect {
public:
    Limiter() = default;
    ~Limiter() override;

    const char* name() const noexcept override;
    size_t parameter_count() const noexcept override;
    const ParameterInfo* parameters() const noexcept override;
    esp_err_t set_parameter(size_t index, float value) noexcept override;
    esp_err_t get_parameter(size_t index, float* value) const noexcept override;
    size_t memory_usage_bytes() const noexcept override;

protected:
    esp_err_t initialize_effect() noexcept override;
    void deinitialize_effect() noexcept override;
    void reset_effect() noexcept override;
    void process_frame(const float* input, float* wet_output) noexcept override;

private:
    struct ChannelState {
        float* samples = nullptr;
        size_t length = 0;
        size_t write_position = 0;
    };

    static constexpr size_t kMaxChannels = 2;
    static constexpr size_t kLookaheadMilliseconds = 5;

    void release_buffers() noexcept;
    void update_release_coefficient() noexcept;

    ChannelState channels_[kMaxChannels]{};
    float* peak_values_ = nullptr;
    size_t* peak_indices_ = nullptr;
    size_t peak_queue_capacity_ = 0;
    size_t peak_queue_head_ = 0;
    size_t peak_queue_tail_ = 0;
    size_t peak_queue_count_ = 0;
    size_t lookahead_frames_ = 0;
    size_t frame_index_ = 0;
    float ceiling_db_ = -1.0f;
    float release_ms_ = 100.0f;
    float release_coefficient_ = 0.0f;
    float gain_ = 1.0f;
    float ceiling_linear_ = 0.89125094f;
    size_t memory_usage_bytes_ = 0;
};

}  // namespace audio_fx
