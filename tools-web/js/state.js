"use strict";

    // Required Firmware Identity Signature
    const FIRMWARE_TOOL_ID = "TOOL_ID:Strg-Alt-Entf-0x00/esp-idf-audio-fx:console_audio_fx:1";

    // Metadata for all 8 DSP Effects
    const EFFECTS_METADATA = {
      reverb: {
        name: "reverb",
        desc: "8 Comb + 4 Allpass Freeverb",
        params: [
          { index: 0, name: "mix", min: 0.0, max: 1.0, default: 0.35, step: 0.01, unit: "" },
          { index: 1, name: "room_size", min: 0.0, max: 1.0, default: 0.5, step: 0.01, unit: "" },
          { index: 2, name: "damping", min: 0.0, max: 0.99, default: 0.25, step: 0.01, unit: "" }
        ]
      },
      chorus: {
        name: "chorus",
        desc: "Dual Stereo LFO Modulation",
        params: [
          { index: 0, name: "mix", min: 0.0, max: 1.0, default: 0.5, step: 0.01, unit: "" },
          { index: 1, name: "rate_hz", min: 0.1, max: 5.0, default: 1.2, step: 0.1, unit: " Hz" },
          { index: 2, name: "depth_ms", min: 0.0, max: 10.0, default: 3.5, step: 0.1, unit: " ms" }
        ]
      },
      equalizer: {
        name: "equalizer",
        desc: "5-Band Parametric Biquad",
        isEq: true,
        params: [
          { index: 0, name: "b1_freq", min: 20, max: 20000, default: 80, step: 10, unit: " Hz" },
          { index: 1, name: "b1_gain", min: -12, max: 12, default: 0, step: 0.5, unit: " dB" },
          { index: 2, name: "b1_q", min: 0.1, max: 10, default: 0.707, step: 0.05, unit: "" },
          { index: 3, name: "b2_freq", min: 20, max: 20000, default: 250, step: 10, unit: " Hz" },
          { index: 4, name: "b2_gain", min: -12, max: 12, default: 0, step: 0.5, unit: " dB" },
          { index: 5, name: "b2_q", min: 0.1, max: 10, default: 0.707, step: 0.05, unit: "" },
          { index: 6, name: "b3_freq", min: 20, max: 20000, default: 1000, step: 20, unit: " Hz" },
          { index: 7, name: "b3_gain", min: -12, max: 12, default: 0, step: 0.5, unit: " dB" },
          { index: 8, name: "b3_q", min: 0.1, max: 10, default: 0.707, step: 0.05, unit: "" },
          { index: 9, name: "b4_freq", min: 20, max: 20000, default: 4000, step: 50, unit: " Hz" },
          { index: 10, name: "b4_gain", min: -12, max: 12, default: 0, step: 0.5, unit: " dB" },
          { index: 11, name: "b4_q", min: 0.1, max: 10, default: 0.707, step: 0.05, unit: "" },
          { index: 12, name: "b5_freq", min: 20, max: 20000, default: 12000, step: 100, unit: " Hz" },
          { index: 13, name: "b5_gain", min: -12, max: 12, default: 0, step: 0.5, unit: " dB" },
          { index: 14, name: "b5_q", min: 0.1, max: 10, default: 0.707, step: 0.05, unit: "" }
        ]
      },
      delay: {
        name: "delay",
        desc: "Feedback Echo (up to 1000 ms)",
        params: [
          { index: 0, name: "mix", min: 0.0, max: 1.0, default: 0.4, step: 0.01, unit: "" },
          { index: 1, name: "time_ms", min: 1.0, max: 1000.0, default: 350.0, step: 5.0, unit: " ms" },
          { index: 2, name: "feedback", min: 0.0, max: 0.95, default: 0.5, step: 0.01, unit: "" }
        ]
      },
      flanger: {
        name: "flanger",
        desc: "Resonant Sweeping Comb",
        params: [
          { index: 0, name: "mix", min: 0.0, max: 1.0, default: 0.5, step: 0.01, unit: "" },
          { index: 1, name: "rate_hz", min: 0.1, max: 5.0, default: 0.5, step: 0.1, unit: " Hz" },
          { index: 2, name: "depth_ms", min: 0.0, max: 4.0, default: 2.0, step: 0.1, unit: " ms" },
          { index: 3, name: "feedback", min: -0.95, max: 0.95, default: 0.6, step: 0.02, unit: "" }
        ]
      },
      bitcrusher: {
        name: "bitcrusher",
        desc: "Lo-Fi Sample & Hold Quantizer",
        params: [
          { index: 0, name: "mix", min: 0.0, max: 1.0, default: 1.0, step: 0.01, unit: "" },
          { index: 1, name: "bit_depth", min: 1, max: 16, default: 8, step: 1, unit: " bits" },
          { index: 2, name: "sample_hold", min: 1, max: 64, default: 4, step: 1, unit: " smp" }
        ]
      },
      compressor: {
        name: "compressor",
        desc: "Linked Dynamics with Soft-Knee",
        params: [
          { index: 0, name: "threshold_db", min: -60.0, max: 0.0, default: -20.0, step: 1.0, unit: " dB" },
          { index: 1, name: "ratio", min: 1.0, max: 20.0, default: 4.0, step: 0.5, unit: ":1" },
          { index: 2, name: "attack_ms", min: 0.1, max: 100.0, default: 10.0, step: 0.5, unit: " ms" },
          { index: 3, name: "release_ms", min: 10.0, max: 1000.0, default: 100.0, step: 5.0, unit: " ms" },
          { index: 4, name: "knee_db", min: 0.0, max: 24.0, default: 6.0, step: 0.5, unit: " dB" },
          { index: 5, name: "makeup_db", min: -12.0, max: 24.0, default: 0.0, step: 0.5, unit: " dB" },
          { index: 6, name: "mix", min: 0.0, max: 1.0, default: 1.0, step: 0.01, unit: "" }
        ]
      },
      limiter: {
        name: "limiter",
        desc: "Brickwall 5ms Lookahead",
        params: [
          { index: 0, name: "ceiling_db", min: -12.0, max: 0.0, default: -0.5, step: 0.1, unit: " dB" },
          { index: 1, name: "release_ms", min: 10.0, max: 1000.0, default: 50.0, step: 5.0, unit: " ms" }
        ]
      }
    };

    // State Variables
    let port = null;
    let reader = null;
    let writer = null;
    let isConnected = false;
    let isStreaming = false;
    let streamType = "STOPPED"; // "STOPPED", "PLAYING", "LIVE_MIC"
    let currentSourceMode = "wav"; // "wav" or "mic"
    let activeEffect = "reverb";
    let isBypassed = false;
    const effectEnabledState = {
      reverb: true,
      chorus: false,
      equalizer: false,
      delay: false,
      flanger: false,
      bitcrusher: false,
      compressor: false,
      limiter: false
    };
    let trackList = []; // Array of { index: number, name: string }
    let currentTrackIndex = 1;
    let currentTrackName = "";
    let currentMic = "inmp441";
    let pollingTimer = null;
    let lineBuffer = "";
    let lastVolume = -30.0;
    let isMuted = false;
    let dmaFramesSim = 0;

    function updateScopeMeta(text) {
      const meta = document.getElementById("scopeStreamMeta");
      if (meta) meta.textContent = text;
    }
