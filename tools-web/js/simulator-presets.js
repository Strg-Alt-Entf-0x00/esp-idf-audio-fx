"use strict";

// --- Offline Simulator Engine ---
    function startSimulatorMode() {
      isConnected = true;
      setUIConnected(true);
      logTerminal("[SIMULATOR] ESP32-P4 Virtual Pipeline Initialized.", "info");
      parseIncomingLine(FIRMWARE_TOOL_ID);
      parseIncomingLine("Playback stopped; selected effect: reverb; volume: -30.0 dB");
      parseIncomingLine("Speaker volume: -30.0 dB");
      parseIncomingLine("Microphone gain: 0.0 dB");
      parseIncomingLine("MIC_CONFIG input=inmp441");
      parseIncomingLine("TRACK index=1 name=acoustic_fingers_test_20s.wav");
      parseIncomingLine("TRACK index=2 name=ambient_warm_chords_test_20s.wav");
      parseIncomingLine("TRACK index=3 name=funk_pocket_groove_test_20s.wav");
      parseIncomingLine("TRACK index=4 name=speech_sample_english_20s.wav");
      parseIncomingLine("Bypass: disabled");
      sendSerialCommand("fx params");
      startPolling();
    }

    function handleSimulatorCommand(cmd) {
      const parts = cmd.trim().split(/\s+/);
      const action = parts[0];

      if (action === "tool_id") {
        setTimeout(() => parseIncomingLine(FIRMWARE_TOOL_ID), 50);
      } else if (action === "tracks") {
        setTimeout(() => {
          parseIncomingLine("TRACK index=1 name=acoustic_fingers_test_20s.wav");
          parseIncomingLine("TRACK index=2 name=ambient_warm_chords_test_20s.wav");
          parseIncomingLine("TRACK index=3 name=funk_pocket_groove_test_20s.wav");
          parseIncomingLine("TRACK index=4 name=speech_sample_english_20s.wav");
        }, 50);
      } else if (action === "play") {
        const idx = parseInt(parts[1], 10) || 1;
        const tr = trackList.find(t => t.index === idx) || { index: idx, name: "track_" + idx + ".wav" };
        setTimeout(() => {
          parseIncomingLine(`Playing track ${idx}: ${tr.name}; effect: ${activeEffect}; volume: ${lastVolume.toFixed(1)} dB`);
        }, 80);
      } else if (action === "stop") {
        setTimeout(() => {
          parseIncomingLine(`Playback stopped; selected effect: ${activeEffect}; volume: ${lastVolume.toFixed(1)} dB`);
        }, 80);
      } else if (action === "audio" && parts[1] === "start") {
        setTimeout(() => {
          parseIncomingLine(`Live microphone: ${currentMic}; effect: ${activeEffect}; volume: ${lastVolume.toFixed(1)} dB`);
        }, 80);
      } else if (action === "volume") {
        const v = parseFloat(parts[1]) || -30;
        setTimeout(() => parseIncomingLine(`Speaker volume: ${v.toFixed(1)} dB`), 40);
      } else if (action === "mic_select") {
        const m = parts[1] || "inmp441";
        setTimeout(() => parseIncomingLine(`MIC_CONFIG input=${m}`), 40);
      } else if (action === "mic_gain") {
        const g = parseFloat(parts[1]) || 0;
        setTimeout(() => parseIncomingLine(`Microphone gain: ${g.toFixed(1)} dB`), 40);
      } else if (action === "fx" && parts[1] === "bypass") {
        const b = parts[2] === "on";
        setTimeout(() => parseIncomingLine(b ? "Bypass: enabled" : "Bypass: disabled"), 40);
      } else if (action === "fx" && parts[1] === "params") {
        setTimeout(() => {
          const fx = EFFECTS_METADATA[activeEffect];
          if (fx) {
            fx.params.forEach(p => {
              parseIncomingLine(`FX_PARAM index=${p.index} name=${p.name} current=${p.default} min=${p.min} max=${p.max} default=${p.default}`);
            });
          }
        }, 80);
      } else if (action === "diag") {
        dmaFramesSim += 240;
        const peak = isStreaming ? Math.floor(Math.random() * 22000 + 4000) : 0;
        setTimeout(() => {
          parseIncomingLine(`DMA completed=${dmaFramesSim} bytes=${dmaFramesSim * 4} non_silent=${isStreaming ? 240 : 0} silent=${isStreaming ? 0 : 240} peak_sample=${peak} buffer_bytes=960`);
        }, 60);
      }
    }

    // --- Curated Presets ---
    const PRESETS = {
      cathedral: {
        fx: "reverb",
        params: { 0: 0.65, 1: 0.85, 2: 0.15 }
      },
      shimmer: {
        fx: "chorus",
        params: { 0: 0.70, 1: 0.8, 2: 4.2 }
      },
      slapback: {
        fx: "delay",
        params: { 0: 0.45, 1: 120.0, 2: 0.25 }
      },
      lofi: {
        fx: "bitcrusher",
        params: { 0: 1.0, 1: 6, 2: 8 }
      },
      warmVocal: {
        fx: "equalizer",
        params: { 1: -4.0, 4: +2.0, 7: +1.0, 10: +3.5, 13: +2.0 }
      },
      broadcast: {
        fx: "compressor",
        params: { 0: -18.0, 1: 4.0, 2: 10.0, 3: 120.0, 4: 8.0, 5: 3.5, 6: 1.0 }
      }
    };

    function applyPreset(name) {
      const p = PRESETS[name];
      if (!p) return;
      selectEffect(p.fx, true);
      setTimeout(() => {
        for (const [idx, val] of Object.entries(p.params)) {
          const slider = document.getElementById(`param_slider_${idx}`);
          if (slider) {
            slider.value = val;
            const fx = EFFECTS_METADATA[p.fx];
            const paramDef = fx.params.find(item => item.index === parseInt(idx, 10));
            const unit = paramDef ? paramDef.unit : "";
            onParamSliderInput(idx, val, unit);
          }
        }
        logTerminal(`[PRESET] Loaded curated sound profile: "${name.toUpperCase()}"`, "info");
      }, 150);
    }

    function exportPresetJson() {
      const fx = EFFECTS_METADATA[activeEffect];
      const payload = {
        name: `Audio FX - ${fx.name}`,
        activeEffect: activeEffect,
        effectStates: { ...effectEnabledState },
        bypassed: isBypassed,
        timestamp: new Date().toISOString(),
        parameters: {}
      };
      fx.params.forEach(p => {
        const slider = document.getElementById(`param_slider_${p.index}`);
        payload.parameters[p.name] = slider ? parseFloat(slider.value) : p.default;
      });
      const dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(payload, null, 2));
      const a = document.createElement("a");
      a.href = dataStr;
      a.download = `audio_fx_${activeEffect}_preset.json`;
      a.click();
      logTerminal(`[PRESET] Exported JSON preset for ${activeEffect.toUpperCase()}`, "info");
    }

    function importPresetJsonPrompt() {
      const fileInput = document.getElementById("jsonFileInput");
      if (fileInput) {
        fileInput.value = "";
        fileInput.click();
      } else {
        const raw = prompt("Paste Audio FX JSON Profile contents below:");
        if (raw) applyImportedJson(raw);
      }
    }

    function handlePresetFileImport(e) {
      const file = e.target.files?.[0];
      if (!file) return;
      const r = new FileReader();
      r.onload = (evt) => {
        applyImportedJson(evt.target.result);
      };
      r.readAsText(file);
    }

    function applyImportedJson(raw) {
      try {
        const parsed = JSON.parse(raw);
        if (parsed.effectStates && typeof parsed.effectStates === "object") {
          Object.assign(effectEnabledState, parsed.effectStates);
        }
        const eff = parsed.activeEffect || parsed.effect;
        if (eff && EFFECTS_METADATA[eff]) {
          if (parsed.bypassed !== undefined) {
            effectEnabledState[eff] = !parsed.bypassed;
          }
          selectEffect(eff, true);
          setTimeout(() => {
            const fx = EFFECTS_METADATA[eff];
            fx.params.forEach(p => {
              if (parsed.parameters && parsed.parameters[p.name] !== undefined) {
                const val = parsed.parameters[p.name];
                const slider = document.getElementById(`param_slider_${p.index}`);
                if (slider) {
                  slider.value = val;
                  onParamSliderInput(p.index, val, p.unit);
                }
              }
            });
            logTerminal(`[PRESET] Successfully imported preset "${parsed.name || eff.toUpperCase()}"`, "info");
          }, 150);
        } else {
          alert("Invalid or unrecognized DSP effect preset JSON.");
        }
      } catch (err) {
        alert("JSON syntax error: " + err.message);
      }
    }
