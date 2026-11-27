"use strict";

// --- Web Serial API Implementation ---
    async function toggleConnection() {
      if (isConnected) {
        await disconnectSerial();
      } else {
        await connectSerial();
      }
    }

    async function connectSerial() {
      if (!("serial" in navigator)) {
        logTerminal("[ERR] Web Serial API not supported in this browser. Falling back to offline simulator mode.", "err");
        startSimulatorMode();
        return;
      }

      try {
        port = await navigator.serial.requestPort();
        const baudRate = 115200;
        await port.open({ baudRate: baudRate });

        isConnected = true;
        setUIConnected(true);
        logTerminal(`[SYS] Serial port opened at ${baudRate} Baud. Handshaking...`, "info");

        startReadingLoop();

        // Handshake commands
        await sendSerialRaw("\r\n");
        setTimeout(async () => {
          await sendSerialRaw("tool_id\r\n");
          queryDeviceStatus();
        }, 300);

        startPolling();
      } catch (err) {
        logTerminal(`[ERR] Serial connection aborted: ${err.message}`, "err");
        setUIConnected(false);
      }
    }

    async function disconnectSerial() {
      try {
        if (isStreaming) {
          await sendSerialRaw("stop\r\n");
        }
        stopPolling();
        if (reader) {
          await reader.cancel();
          reader = null;
        }
        if (writer) {
          writer.releaseLock();
          writer = null;
        }
        if (port) {
          await port.close();
          port = null;
        }
      } catch (e) {
        console.warn(e);
      } finally {
        isConnected = false;
        setUIConnected(false);
        logTerminal("[SYS] Serial port disconnected.", "info");
      }
    }

    async function startReadingLoop() {
      const textDecoder = new TextDecoderStream();
      port.readable.pipeTo(textDecoder.writable);
      reader = textDecoder.readable.getReader();

      try {
        while (true) {
          const { value, done } = await reader.read();
          if (done) break;
          if (value) {
            lineBuffer += value;
            const lines = lineBuffer.split(/\r?\n/);
            lineBuffer = lines.pop();
            for (const line of lines) {
              const tr = line.trim();
              if (tr) parseIncomingLine(tr);
            }
          }
        }
      } catch (err) {
        console.error("Read loop error", err);
      } finally {
        if (reader) try { reader.releaseLock(); } catch(e) {}
      }
    }

    function parseIncomingLine(line) {
      logTerminal(line, "rx");

      if (line.includes(FIRMWARE_TOOL_ID)) {
        logTerminal("[SYS] Verified Firmware Tool ID: Strg-Alt-Entf-0x00/esp-idf-audio-fx:console_audio_fx:1 Online", "info");
      }

      // Audio Stream Status
      if (line.includes("Audio stream: stopped") || line.includes("Playback stopped;")) {
        setStreamingState("STOPPED");
      } else if (line.includes("Audio stream: active") || line.includes("Audio stream: playing")) {
        setAudioSourceMode("wav", true);
        setStreamingState("PLAYING");
      } else if (line.includes("Audio stream: live microphone input") || line.includes("Audio stream: live_mic")) {
        setAudioSourceMode("mic", true);
        setStreamingState("LIVE MIC");
      }

      // Playing track X: name; effect: Y; volume: Z dB
      const playMatch = line.match(/Playing track (\d+):\s*(.*?);\s*effect:\s*(\w+);\s*volume:\s*([-+0-9.]+)\s*dB/i);
      if (playMatch) {
        const idx = parseInt(playMatch[1], 10);
        const name = playMatch[2].trim();
        currentTrackIndex = idx;
        currentTrackName = name;
        addTrackOption(idx, name);
        setAudioSourceMode("wav", true);
        setStreamingState("PLAYING", name);
      }

      // Live microphone: X; effect: Y; volume: Z dB
      const liveMicMatch = line.match(/Live microphone:\s*(\w+);\s*effect:\s*(\w+);\s*volume:\s*([-+0-9.]+)\s*dB/i);
      if (liveMicMatch) {
        const mic = liveMicMatch[1].toLowerCase();
        applyMicSelection(mic);
        setAudioSourceMode("mic", true);
        setStreamingState("LIVE MIC", mic.toUpperCase());
      }

      // Audio stream: WAV track X
      const wavMatch = line.match(/Audio stream: WAV track (\d+)/i);
      if (wavMatch) {
        currentTrackIndex = parseInt(wavMatch[1], 10);
        setAudioSourceMode("wav", true);
        setStreamingState("PLAYING");
      }

      // Volume
      const volMatch = line.match(/Speaker volume:\s*([-+0-9.]+)\s*dB/i);
      if (volMatch) {
        const vol = parseFloat(volMatch[1]);
        const slider = document.getElementById("volumeSlider");
        if (slider) slider.value = vol;
        const vVal = document.getElementById("volumeVal");
        if (vVal) vVal.textContent = `${vol.toFixed(1)} dB`;
      }

      // Codec Mic Gain
      const gainMatch = line.match(/Microphone gain:\s*([-+0-9.]+)\s*dB/i);
      if (gainMatch) {
        const gain = parseFloat(gainMatch[1]);
        const slider = document.getElementById("micGainSlider");
        if (slider) slider.value = gain;
        const gVal = document.getElementById("micGainVal");
        if (gVal) gVal.textContent = `${gain.toFixed(1)} dB`;
      }

      // Mic Input Selection
      if (line.startsWith("MIC_CONFIG input=")) {
        const input = line.split("=")[1].trim().toLowerCase();
        applyMicSelection(input);
      }

      // Tracks
      if (line.includes("No WAV tracks staged")) {
        trackList = [];
        const sel = document.getElementById("trackSelect");
        if (sel) sel.innerHTML = '<option value="">No WAV tracks found on flash</option>';
        logTerminal("[FS] No WAV tracks staged on flash partition.", "info");
      }
      const trackMatch = line.match(/TRACK index=(\d+)\s+name=(.*)/i);
      if (trackMatch) {
        addTrackOption(parseInt(trackMatch[1], 10), trackMatch[2].trim());
      }

      // Bypass Status
      if (line.includes("Bypass: enabled") || line.includes("bypass: on") || line.includes("bypassed=yes")) {
        setBypassUI(true);
      } else if (line.includes("Bypass: disabled") || line.includes("bypass: off") || line.includes("bypassed=no")) {
        setBypassUI(false);
      }

      // FX_PARAM parsing: FX_PARAM index=0 name=mix current=0.35 min=0 max=1 default=0.35
      const paramMatch = line.match(/FX_PARAM index=(\d+) name=([A-Za-z0-9_.-]+) current=([-+0-9.eE]+) min=([-+0-9.eE]+) max=([-+0-9.eE]+) default=([-+0-9.eE]+)/i);
      if (paramMatch) {
        const idx = parseInt(paramMatch[1], 10);
        const currentVal = parseFloat(paramMatch[3]);
        const slider = document.getElementById(`param_slider_${idx}`);
        const label = document.getElementById(`val_label_${idx}`);
        if (slider) slider.value = currentVal;
        if (label) {
          const fx = EFFECTS_METADATA[activeEffect];
          const p = fx ? fx.params.find(param => param.index === idx) : null;
          const unit = p ? p.unit : "";
          if (unit.trim() === "dB") {
            const sign = currentVal > 0 ? "+" : "";
            label.textContent = `${sign}${currentVal.toFixed(1)}${unit}`;
          } else {
            label.textContent = `${currentVal.toFixed(2)}${unit}`;
          }
        }
      }

      // Firmware Diag: DMA completed=%u bytes=%u non_silent=%u silent=%u peak_sample=%u buffer_bytes=%u
      const dmaDiagMatch = line.match(/DMA completed=(\d+)\s+bytes=(\d+)\s+non_silent=(\d+)\s+silent=(\d+)\s+peak_sample=(\d+)\s+buffer_bytes=(\d+)/i);
      if (dmaDiagMatch) {
        const completed = parseInt(dmaDiagMatch[1], 10);
        const peakSample = parseInt(dmaDiagMatch[5], 10);
        const dmaEl = document.getElementById("dmaFramesVal");
        if (dmaEl) dmaEl.textContent = completed.toLocaleString();

        const meterDma = document.getElementById("meterDma");
        if (meterDma) {
          const pct = Math.min(100, Math.max(0, (peakSample / 32767) * 100));
          meterDma.style.width = `${pct}%`;
        }

        if (peakSample > 0) {
          const db = 20 * Math.log10(peakSample / 32767);
          updateMeter("meterOut", "rmsOutVal", db);
          if (isStreaming) {
            updateMeter("meterIn", "rmsInVal", db - 2.5);
          }
        } else if (!isStreaming) {
          updateMeter("meterOut", "rmsOutVal", -90);
          updateMeter("meterIn", "rmsInVal", -90);
        }
      }

      // Diag metrics (legacy or simulator format)
      const diagRmsIn = line.match(/rms_in=([-+0-9.eE]+)/i);
      if (diagRmsIn) updateMeter("meterIn", "rmsInVal", parseFloat(diagRmsIn[1]));
      const diagRmsOut = line.match(/rms_out=([-+0-9.eE]+)/i);
      if (diagRmsOut) updateMeter("meterOut", "rmsOutVal", parseFloat(diagRmsOut[1]));
      const dmaFrames = line.match(/dma_frames=(\d+)/i);
      if (dmaFrames) {
        const dmaEl = document.getElementById("dmaFramesVal");
        if (dmaEl) dmaEl.textContent = dmaFrames[1];
      }
    }

    async function sendSerialCommand(cmd) {
      logTerminal(`TX >> ${cmd}`, "tx");
      if (!isConnected || !port || !port.writable) {
        handleSimulatorCommand(cmd);
        return;
      }
      await sendSerialRaw(cmd + "\r\n");
    }

    async function sendSerialRaw(text) {
      if (!port || !port.writable) return;
      const encoder = new TextEncoder();
      const w = port.writable.getWriter();
      await w.write(encoder.encode(text));
      w.releaseLock();
    }

    function queryDeviceStatus() {
      const cmds = [
        "tracks",
        "status",
        "audio status",
        "volume",
        "mic_select",
        "mic_gain",
        "fx list",
        "fx params",
        "fx bypass status",
        "diag"
      ];
      cmds.forEach((c, i) => setTimeout(() => sendSerialCommand(c), i * 80));
    }

    function startPolling() {
      if (pollingTimer) clearInterval(pollingTimer);
      pollingTimer = setInterval(() => {
        if (isConnected) {
          sendSerialCommand("audio status");
          sendSerialCommand("diag");
        }
      }, 2500);
    }

    function stopPolling() {
      if (pollingTimer) {
        clearInterval(pollingTimer);
        pollingTimer = null;
      }
    }

    function setUIConnected(connected) {
      const btn = document.getElementById("connectBtn");
      const tLabel = document.getElementById("transportStateLabel");
      const mLabel = document.getElementById("micStateLabel");
      if (connected) {
        btn.classList.add("connected");
        btn.dataset.tooltip = "Connected: ESP32-P4 Audio FX (Click to Disconnect)";
        updateScopeMeta("Hardware Ready · 48.0 kHz · Stereo");
        if (tLabel) tLabel.textContent = "Hardware Ready";
        if (mLabel) mLabel.textContent = "Standby";
      } else {
        btn.classList.remove("connected");
        btn.dataset.tooltip = "Connect ESP32 (115200 Baud)";
        setStreamingState("STOPPED");
        updateScopeMeta("Standby · Offline");
        if (tLabel) tLabel.textContent = "Standby (Offline)";
        if (mLabel) mLabel.textContent = "Offline";
      }
    }

    function refreshTracks() {
      trackList = [];
      const sel = document.getElementById("trackSelect");
      if (sel) {
        sel.innerHTML = '<option value="">Scanning flash tracks...</option>';
      }
      logTerminal("[FS] Querying flash WAV tracks (tracks)...", "info");
      sendSerialCommand("tracks");
    }

    function addTrackOption(index, name) {
      if (!trackList.some(t => t.index === index)) {
        trackList.push({ index, name });
        trackList.sort((a, b) => a.index - b.index);
      }
      const select = document.getElementById("trackSelect");
      if (!select) return;
      select.innerHTML = "";
      trackList.forEach(t => {
        const opt = document.createElement("option");
        opt.value = t.index;
        opt.textContent = `[${t.index}] ${t.name}`;
        if (t.index === currentTrackIndex) opt.selected = true;
        select.appendChild(opt);
      });
      if (!select.value && trackList.length > 0) {
        select.value = trackList[0].index;
        currentTrackIndex = trackList[0].index;
        currentTrackName = trackList[0].name;
      }
    }

    function updateMeter(barId, labelId, dbVal) {
      const label = document.getElementById(labelId);
      const bar = document.getElementById(barId);
      if (!label || !bar) return;

      if (isNaN(dbVal) || dbVal <= -89) {
        label.textContent = "-inf dB";
        bar.style.width = "0%";
      } else {
        label.textContent = `${dbVal.toFixed(1)} dB`;
        const pct = Math.max(0, Math.min(100, ((dbVal + 60) / 60) * 100));
        bar.style.width = `${pct}%`;
      }
    }

    function sendManualCommand() {
      const input = document.getElementById("cmdInput");
      const cmd = input.value.trim();
      if (cmd) {
        sendSerialCommand(cmd);
        input.value = "";
      }
    }

    function logTerminal(msg, type = "info") {
      const box = document.getElementById("terminalLog");
      if (!box) return;
      const el = document.createElement("div");
      el.className = `log-entry log-${type}`;
      el.textContent = msg;
      box.appendChild(el);
      box.scrollTop = box.scrollHeight;
      while (box.children.length > 250) {
        box.removeChild(box.firstChild);
      }
    }

    function clearLog() {
      const box = document.getElementById("terminalLog");
      if (box) box.innerHTML = "";
    }
