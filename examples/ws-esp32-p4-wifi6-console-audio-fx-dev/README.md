# Audio FX console and desktop tool

This standalone Waveshare ESP32-P4-WIFI6 Rev. 1.x application combines the
local WAV player, live microphone path, audio effects, serial console, and DMA
diagnostics in one developer example. The Python desktop tool discovers
controls from the running firmware and verifies its identity before enabling
device access.

The firmware supports one selected effect at a time; it does not implement a
multi-effect chain.

## Local WAV files

Put WAV files you are licensed to use in the repository's ignored `.data`
directory. Folders are kept in the names shown by the player:

```text
.data\
  music\
    acoustic.wav
  speech\
    narration.wav
```

Inputs must be uncompressed PCM, 16-bit little-endian, 48 kHz, mono or stereo.
The build validates the files and stages them across two 15 MiB SPIFFS
partitions. At most 32 tracks and 20 MiB of audio are supported, with a 10 MiB
payload limit per filesystem. The source files are not changed. The `.data`
folder is not tracked or packaged; users provide their own audio.

Convert audio you have rights to use with FFmpeg, for example:

```powershell
ffmpeg -i input.mp3 -map 0:a:0 -c:a pcm_s16le -ar 48000 output.wav
```

No WAV files is a supported configuration; the player then boots with an
empty track list. Adding, removing, or renaming WAV files triggers staging on
the next CMake configure/build.

## Build and flash

Run these commands from an ESP-IDF 6.0 environment:

```powershell
cd examples\console_audio_fx
idf.py build
idf.py -p COM4 flash monitor
```

The flash action writes the application and both generated SPIFFS images.
Building alone does not write to the board. The partition table dedicates two
15 MiB SPIFFS filesystems and a 1 MiB factory application; this test image does
not include OTA slots.

The speaker starts at `-30 dB`. Start at low volume and use suitable
headphones or a low-level speaker setup. The console and desktop controls
accept speaker volume from `-60` through `0 dB`.

## Web Studio application

Open `tools-web/audio_fx_studio.html` directly in any Web Serial-capable browser (Google Chrome, Microsoft Edge, Opera). No installation, Python environment, or drivers required.

Click **Connect ESP32**, select your serial port at 115200 baud, and the studio automatically handshakes with the firmware. The interface provides:

- **Playback & Live Mic**: Instant play/stop for onboard WAV tracks and live microphone streaming.
- **Hardware Controls**: Speaker volume (-60 to 0 dB), input selection (INMP441 vs ES8311 Codec), and Codec mic gain.
- **DSP Effect Rack**: Select any of the 8 effects, tweak parameters with smooth real-time sliders, and toggle bypass.
- **5-Band Parametric EQ**: Interactive visualizer with vertical gain sliders and coefficient ramping.
- **Real-Time Telemetry & Scope**: Live oscilloscope canvas, input/output RMS meters, and raw command terminal.

## Firmware console

The UART console is also available directly at 115200 baud. Useful commands:

```text
help
tool_id
tracks
fx list
fx select reverb
fx params
fx set 1 0.6
fx bypass on
fx bypass status
volume -30
volume
mic_select
stop
mic_select codec
mic_gain 12
audio start
audio status
play 1
diag
stop
fx select dry
```

`tracks` reports the track numbers. `fx params` prints machine-readable
`FX_PARAM` lines, and `diag` reports the I2S TX DMA counters. Each accepted
state-changing console command waits for the audio task to apply it before
returning to the prompt.

## Listening diagnostics

For a guided listening sequence, run the optional serial test script from the
example's `tools-py` directory:

```powershell
python tools-py\listen_test.py --port COM4
```

The script asks before flashing, then plays curated track/effect samples.
`--no-flash` uses the image already installed; `--auto` runs without waiting
between samples; `--volume -35` selects another safe starting level. Use
`Ctrl+C` to stop. After each sample the device reports source/processed
RMS and peak, invalid/clipped samples, I2S bytes accepted, and completed
non-silent TX DMA buffers.

These digital diagnostics can establish that samples reached the I2S DMA path;
they cannot prove that the codec, amplifier, speaker, or headphones produced
audible sound. Confirm the acoustic output separately at a safe level.

The host-side console parser and WAV-staging checks can be run without a board:

```powershell
python -m unittest discover -s tools-py\tests -t tools-py -v
python -m unittest discover -s tests -v
```
