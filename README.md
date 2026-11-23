# ESP-IDF Audio FX

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.0%2B%20%7C%20v6.0%2B-blue.svg)](https://github.com/espressif/esp-idf)
[![Target](https://img.shields.io/badge/Target-ESP32%20%7C%20S3%20%7C%20P4-orange.svg)](https://www.espressif.com/)
[![Web Studio](https://img.shields.io/badge/Studio-Web%20Serial%20Studio-cyan.svg)](tools-web/audio_fx_studio.html)
[![License](https://img.shields.io/badge/License-Non--Commercial%20%2F%20Hobbyist-green.svg)](#license)

Modular, block-based native C++17 audio effects component for ESP-IDF. Provides sample-rate-aware time, modulation, filter, dynamics, and lo-fi effects through an interleaved `float` block API with build-time effect selection.

Designed for high-performance audio systems on ESP32, ESP32-S3, and ESP32-P4 without requiring the heavy ESP-ADF framework.

---

## Implemented Effects (8 DSP Processors)

| Effect | Topology / Architecture | Key Parameters |
|---|---|---|
| **Reverb** | Eight parallel feedback comb filters followed by four allpass diffusion stages (Freeverb) | `mix` (0..1), `room_size` (0..1), `damping` (0..0.99) |
| **Chorus** | Fractional-delay modulation with linear interpolation and 180° opposite LFO phase on stereo channels | `mix` (0..1), `rate_hz` (0.1..5 Hz), `depth_ms` (0..10 ms) |
| **Equalizer** | Five cascaded, independently parameterized peaking biquads with per-channel state and 5 ms anti-click coefficient ramps | 5 bands of `frequency_hz`, `gain_db` (-12..+12 dB), and `q` (0.1..10) |
| **Delay** | Fractional mono/stereo feedback delay (up to 1 sec); delay-time transitions ramp over 20 ms | `mix` (0..1), `time_ms` (1..1000 ms), `feedback` (0..0.95) |
| **Flanger** | Short modulated delay with opposite LFO phase on stereo channels and bounded feedback | `mix` (0..1), `rate_hz` (0.1..5 Hz), `depth_ms` (0..4 ms), `feedback` (-0.95..0.95) |
| **Bitcrusher** | Quantized amplitude and sample-and-hold decimation (lo-fi / retro gaming FX) | `mix` (0..1), `bit_depth` (1..16 bits), `sample_hold` (1..64 samples) |
| **Compressor** | Stereo-linked peak detector, soft knee, attack/release envelope, and makeup gain | `threshold_db` (-60..0 dB), `ratio` (1..20), `attack_ms` (0.1..100 ms), `release_ms` (10..1000 ms), `knee_db`, `makeup_db`, `mix` |
| **Limiter** | Linked-channel sample-peak limiter with fixed 5 ms look-ahead ceiling constraint | `ceiling_db` (-12..0 dB), `release_ms` (10..1000 ms) |

All effects accept mono or stereo interleaved floating-point audio at sample rates from 8 kHz through 96 kHz.

---

## Web Serial FX Studio

A zero-install, single-file browser application is included at:
👉 [`tools-web/audio_fx_studio.html`](tools-web/audio_fx_studio.html)

### Features
- **Plug & Play Web Serial**: Connects directly to ESP32 USB/UART (115200 baud) in Google Chrome, Edge, or Opera.
- **Interactive FX Rack**: Instant selection between all 8 effects, live parameter sliders, and bypass switch.
- **5-Band Parametric EQ Visualizer**: Real-time vertical sliders and curve control.
- **Hardware Controls**: Speaker volume (-60 dB to 0 dB), input selection (INMP441 vs ES8311 Codec), and Codec mic gain.
- **Playback & Live Mic**: Trigger onboard SPIFFS WAV track playback or stream live microphone audio through the DSP rack.
- **Real-Time Telemetry & Oscilloscope**: Dynamic signal scope canvas, RMS input/output VU meters, and raw command terminal.

---

## Installation

Add this component to your ESP-IDF project manifest (`main/idf_component.yml`):

```yaml
dependencies:
  esp-idf-audio-fx:
    git: "https://github.com/Strg-Alt-Entf-0x00/esp-idf-audio-fx.git"
```

Then reconfigure CMake:
```bash
idf.py reconfigure
```

---

## Quick Start (C++17)

```cpp
#include <array>
#include <memory>
#include "audio_fx/audio_fx.hpp"

// 1. Define audio format (Sample rate, channels)
audio_fx::AudioFormat format{16000, 1}; // 16 kHz Mono

// 2. Instantiate effect using the factory
std::unique_ptr<audio_fx::Effect> reverb;
ESP_ERROR_CHECK(audio_fx::create_effect(audio_fx::EffectType::reverb, format, &reverb));

// 3. Set parameters
ESP_ERROR_CHECK(reverb->set_parameter(0, 0.35f)); // Mix: 35% wet
ESP_ERROR_CHECK(reverb->set_parameter(1, 0.70f)); // Room Size: 70%

// 4. Process real-time float PCM blocks
std::array<float, 160> input_samples{};
std::array<float, 160> output_samples{};

// In-place processing is also supported: reverb->process(buf, buf, count);
ESP_ERROR_CHECK(reverb->process(input_samples.data(), output_samples.data(), 160));
```

---

## Real-Time & Thread Safety Guidelines

- **Pre-allocate:** Create and configure effect instances during setup before starting the audio DMA task.
- **Zero Allocations:** `process()` allocates no memory and acquires no locks. It is real-time safe for high-priority audio tasks.
- **Buffer Invariance:** Input and output buffers may be identical (in-place processing) or completely separate; non-identical overlapping buffers are forbidden.
- **Parameter Modulation:** Use `set_parameter()` to adjust controls; transitions feature internal gain/coefficient ramping to avoid clicks.

---

## Performance Benchmark (ESP32-P4 @ 360 MHz)

Single-core CPU load measured processing 48 kHz stereo blocks:

| Effect | Measured Core Load |
|---|---:|
| Reverb | 17.25% |
| Chorus | 9.49% |
| Equalizer (5-Band) | 10.42% |
| Delay | 4.11% |
| Flanger | 9.66% |
| Bitcrusher | 2.52% |
| Compressor | 6.75% |
| Limiter | 4.50% |

---

## Testing

```bash
# 1. Run web studio validation tests (Host Python test)
pytest tests/test_web_audio_fx_studio.py

# 2. Build and run on-target Unity test suite (ESP32-P4)
cd tests/target
idf.py build flash monitor
```

---

## License

This project is licensed under the [PolyForm Noncommercial License 1.0.0](LICENSE).
- **Free for noncommercial use**: Personal, educational, research, and hobbyist projects are completely free.
- **Commercial use**: Not permitted without prior authorization. For commercial inquiries, please contact the author via GitHub.
