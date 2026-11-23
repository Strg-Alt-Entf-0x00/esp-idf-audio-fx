#include "wav_reader.hpp"

#include <array>
#include <cstring>

namespace audio_fx_demo {
namespace {

constexpr uint32_t kSupportedSampleRateHz = 48000;
constexpr uint16_t kPcmFormat = 1;
constexpr uint16_t kBitsPerSample = 16;
constexpr size_t kMaximumChunkCount = 64;

uint16_t read_u16_le(const uint8_t* bytes) noexcept {
    return static_cast<uint16_t>(bytes[0]) |
           (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t read_u32_le(const uint8_t* bytes) noexcept {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

bool read_exact(FILE* file, void* data, size_t size) noexcept {
    return std::fread(data, 1, size, file) == size;
}

}  // namespace

WavReader::~WavReader() {
    close();
}

esp_err_t WavReader::open(const char* path) noexcept {
    close();
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }

    file_ = std::fopen(path, "rb");
    if (!file_) {
        return ESP_ERR_NOT_FOUND;
    }

    std::array<uint8_t, 12> header{};
    if (!read_exact(file_, header.data(), header.size()) ||
        std::memcmp(header.data(), "RIFF", 4) != 0 ||
        std::memcmp(header.data() + 8, "WAVE", 4) != 0) {
        close();
        return ESP_ERR_NOT_SUPPORTED;
    }
    const uint32_t riff_size = read_u32_le(header.data() + 4);
    if (riff_size < 4) {
        close();
        return ESP_ERR_INVALID_SIZE;
    }

    if (std::fseek(file_, 0, SEEK_END) != 0) {
        close();
        return ESP_FAIL;
    }
    const long file_size = std::ftell(file_);
    const uint64_t riff_end =
        static_cast<uint64_t>(riff_size) + sizeof(uint32_t) * 2;
    if (file_size < static_cast<long>(header.size()) ||
        riff_end > static_cast<uint64_t>(file_size) ||
        std::fseek(file_, static_cast<long>(header.size()), SEEK_SET) != 0) {
        close();
        return ESP_ERR_INVALID_SIZE;
    }

    bool found_format = false;
    bool found_data = false;
    uint16_t parsed_channels = 0;
    uint16_t parsed_block_align = 0;
    uint32_t parsed_data_size = 0;
    long parsed_data_offset = 0;

    for (size_t chunk_index = 0; chunk_index < kMaximumChunkCount;
         ++chunk_index) {
        const long chunk_header_offset = std::ftell(file_);
        if (chunk_header_offset < 0 ||
            static_cast<uint64_t>(chunk_header_offset) + 8 > riff_end) {
            break;
        }

        std::array<uint8_t, 8> chunk_header{};
        if (!read_exact(file_, chunk_header.data(), chunk_header.size())) {
            close();
            return ESP_ERR_INVALID_SIZE;
        }
        const uint32_t chunk_size = read_u32_le(chunk_header.data() + 4);
        const long chunk_data_offset = std::ftell(file_);
        if (chunk_data_offset < 0 ||
            static_cast<uint64_t>(chunk_size) >
                riff_end - static_cast<uint64_t>(chunk_data_offset)) {
            close();
            return ESP_ERR_INVALID_SIZE;
        }

        if (std::memcmp(chunk_header.data(), "fmt ", 4) == 0) {
            if (found_format || chunk_size < 16) {
                close();
                return ESP_ERR_INVALID_SIZE;
            }
            std::array<uint8_t, 16> format{};
            if (!read_exact(file_, format.data(), format.size())) {
                close();
                return ESP_ERR_INVALID_SIZE;
            }
            const uint16_t audio_format = read_u16_le(format.data());
            parsed_channels = read_u16_le(format.data() + 2);
            const uint32_t sample_rate = read_u32_le(format.data() + 4);
            const uint32_t byte_rate = read_u32_le(format.data() + 8);
            parsed_block_align = read_u16_le(format.data() + 12);
            const uint16_t bits_per_sample =
                read_u16_le(format.data() + 14);
            if (audio_format != kPcmFormat ||
                (parsed_channels != 1 && parsed_channels != 2) ||
                sample_rate != kSupportedSampleRateHz ||
                bits_per_sample != kBitsPerSample ||
                parsed_block_align != parsed_channels * sizeof(int16_t) ||
                byte_rate != sample_rate * parsed_block_align) {
                close();
                return ESP_ERR_NOT_SUPPORTED;
            }
            found_format = true;
        } else if (std::memcmp(chunk_header.data(), "data", 4) == 0) {
            if (found_data) {
                close();
                return ESP_ERR_INVALID_SIZE;
            }
            parsed_data_offset = chunk_data_offset;
            parsed_data_size = chunk_size;
            found_data = true;
        }

        const uint64_t next_chunk =
            static_cast<uint64_t>(chunk_data_offset) + chunk_size +
            (chunk_size & 1U);
        if (next_chunk > riff_end ||
            std::fseek(file_, static_cast<long>(next_chunk), SEEK_SET) != 0) {
            close();
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (!found_format || !found_data || parsed_data_size == 0 ||
        parsed_data_size % parsed_block_align != 0 ||
        std::fseek(file_, parsed_data_offset, SEEK_SET) != 0) {
        close();
        return ESP_ERR_INVALID_SIZE;
    }

    channels_ = parsed_channels;
    block_align_ = parsed_block_align;
    data_remaining_ = parsed_data_size;
    return ESP_OK;
}

esp_err_t WavReader::read_stereo(float* interleaved_output,
                                 size_t max_frames,
                                 size_t* frames_read) noexcept {
    if (!file_ || !frames_read || (max_frames > 0 && !interleaved_output)) {
        return ESP_ERR_INVALID_ARG;
    }
    *frames_read = 0;
    if (max_frames == 0 || data_remaining_ == 0) {
        return ESP_OK;
    }

    constexpr size_t kMaximumFramesPerRead = 240;
    if (max_frames > kMaximumFramesPerRead) {
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t available_frames = data_remaining_ / block_align_;
    const size_t frames =
        available_frames < max_frames ? available_frames : max_frames;
    std::array<uint8_t, kMaximumFramesPerRead * 2 * sizeof(int16_t)> samples{};
    const size_t byte_count = frames * block_align_;
    if (!read_exact(file_, samples.data(), byte_count)) {
        return ESP_FAIL;
    }
    data_remaining_ -= static_cast<uint32_t>(byte_count);

    for (size_t frame = 0; frame < frames; ++frame) {
        const size_t input_offset = frame * block_align_;
        const uint16_t left_raw = read_u16_le(samples.data() + input_offset);
        const int16_t left = static_cast<int16_t>(
            left_raw < 0x8000U ? static_cast<int32_t>(left_raw)
                               : static_cast<int32_t>(left_raw) - 0x10000);
        int16_t right = left;
        if (channels_ == 2) {
            const uint16_t right_raw = read_u16_le(
                samples.data() + input_offset + sizeof(int16_t));
            right = static_cast<int16_t>(
                right_raw < 0x8000U ? static_cast<int32_t>(right_raw)
                                    : static_cast<int32_t>(right_raw) -
                                          0x10000);
        }
        interleaved_output[frame * 2] =
            static_cast<float>(left) / 32768.0f;
        interleaved_output[frame * 2 + 1] =
            static_cast<float>(right) / 32768.0f;
    }
    *frames_read = frames;
    return ESP_OK;
}

void WavReader::close() noexcept {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    channels_ = 0;
    block_align_ = 0;
    data_remaining_ = 0;
}

bool WavReader::is_open() const noexcept {
    return file_ != nullptr;
}

}  // namespace audio_fx_demo
