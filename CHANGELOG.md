## Unreleased

- Reorganized the browser console as the modular Audio FX Studio in
  `tools-web/`, with Web Serial controls for compatible firmware and an
  offline simulator clearly distinguished from device telemetry and audio.

## 0.1.0 - 2026-11-02

- Establish the initial ESP-IDF component and block-processing C++ API.
- Add configurable native reverb and chorus effects with sample-rate-aware
  state allocation and optional Kconfig inclusion.
- Add a portable processing example and Unity test application.
- Add ESP32-P4 revision 1.x defaults for the example and test app; verify both
  builds and all five Unity tests on an ESP32-P4 revision 1.3 with 32 MB flash.
- Add an optional Waveshare P4 live console example with selectable ES8311 and
  INMP441 inputs and queued effect controls; verify codec initialization and
  audio stream start/stop on hardware.
- Add an optional five-band parametric EQ with per-channel biquad state,
  parameter validation, and 5 ms smoothed coefficient changes; verify its
  1 kHz/+6 dB response, stereo ramp consistency, and live controls on the P4.
- Disable all effects by default to avoid linking unused DSP; enable required
  effects explicitly in example and test app defaults.
- Report the EQ's sample-rate-specific frequency limit in its parameter
  metadata, matching the validation enforced by the setter.
- Enforce strict ISO C++17 for the component and bundled examples, replacing
  C++20-only designated initializers.
- Add optional mono/stereo fractional delay and flanger effects, an integer
  bit-depth/sample-hold reducer, a linked-channel soft-knee compressor, and a
  linked sample-peak limiter with fixed 5 ms look-ahead.
- Extend the factory, Kconfig selection, unit tests, and live console to cover
  all eight effects. All 14 Unity tests pass on the ESP32-P4 Rev. 1.3 in two
  runs, including linked-stereo compressor/limiter and limiter ceiling checks;
  the expanded throughput check completes in real time, and the console boots
  and accepts parameter changes for the new effects.
- Record one-run 48 kHz stereo DSP load measurements for all eight effects in
  the README. Listening evaluation and broader target validation remain
  outstanding.
- Extract the Waveshare board driver into an example-only shared component and
  add a separate local-WAV listening-test app with validated `.data` staging,
  two SPIFFS partitions below the filesystem page-index limit, block-streamed
  playback, and queued effect controls. The 15 MiB images build successfully;
  playback-only mode enables the codec I2S RX clock channel as well as TX;
  COM4 verified playback without I2S write timeouts. Keep the output codec
  enabled across track boundaries so consecutive samples remain audible.
- Add a P4 throughput regression check: one second of synthetic 48 kHz stereo
  DSP used approximately 17.2% (reverb), 9.5-9.7% (chorus), and 10.4-10.7%
  (EQ) of one 360 MHz core in repeated measured runs.
- Add per-track source/DSP level, invalid/clipped sample, I2S write, and TX DMA
  completion diagnostics to the WAV player; the automated listening test now
  waits for and evaluates each digital-path PASS/FAIL summary.