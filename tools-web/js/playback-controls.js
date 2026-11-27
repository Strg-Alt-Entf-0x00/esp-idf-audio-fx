"use strict";

// --- Master Bypass & Power Switch Control ---
    function toggleMasterBypass() {
      const newBypass = !isBypassed;
      isBypassed = newBypass;
      effectEnabledState[activeEffect] = !newBypass;

      if (newBypass) {
        sendSerialCommand("fx bypass on");
        logTerminal("[FX] Master FX Bypass engaged (Dry signal)", "warn");
      } else {
        sendSerialCommand(`fx select ${activeEffect}`);
        sendSerialCommand("fx bypass off");
        logTerminal(`[FX] Master FX Bypass disengaged (${activeEffect.toUpperCase()} active)`, "info");
      }

      updateMasterBypassUI();
      updateRackTilesUI();

      const chk = document.getElementById("bypassSwitch");
      if (chk) chk.checked = !isBypassed;
      const lbl = document.getElementById("stageSwitchLabel");
      if (lbl) {
        lbl.textContent = isBypassed ? "OFF" : "ON";
        lbl.style.color = isBypassed ? "var(--text-muted)" : "var(--accent-emerald-soft)";
      }
      const stageParams = document.getElementById("stageParametersContainer");
      if (stageParams) {
        stageParams.style.opacity = isBypassed ? "0.45" : "1.0";
      }
      const diagBypass = document.getElementById("diagBypassState");
      if (diagBypass) diagBypass.textContent = isBypassed ? "ON (BYPASSED)" : "OFF (ACTIVE)";
    }

    function updateMasterBypassUI() {
      const btn = document.getElementById("btnMasterBypass");
      if (btn) {
        btn.classList.toggle("warning-active", isBypassed);
        btn.dataset.tooltip = isBypassed ? "Master FX Bypassed (Click to Enable)" : "Master FX Bypass (A/B Dry/Wet Auditioning)";
      }
    }

    function onEffectPowerSwitch(isOn) {
      effectEnabledState[activeEffect] = isOn;
      isBypassed = !isOn;

      updateMasterBypassUI();
      updateRackTilesUI();

      const lbl = document.getElementById("stageSwitchLabel");
      if (lbl) {
        lbl.textContent = isOn ? "ON" : "OFF";
        lbl.style.color = isOn ? "var(--accent-emerald-soft)" : "var(--text-muted)";
      }
      const stageParams = document.getElementById("stageParametersContainer");
      if (stageParams) {
        stageParams.style.opacity = isOn ? "1.0" : "0.45";
      }

      if (isOn) {
        sendSerialCommand(`fx select ${activeEffect}`);
        sendSerialCommand("fx bypass off");
      } else {
        sendSerialCommand("fx bypass on");
      }

      const diagBypass = document.getElementById("diagBypassState");
      if (diagBypass) diagBypass.textContent = isOn ? "OFF (ACTIVE)" : "ON (BYPASSED)";
    }

    function toggleBypass(checked) {
      // checked = true means BYPASSED (OFF), checked = false means ACTIVE (ON)
      onEffectPowerSwitch(!checked);
    }

    function setBypassUI(bypassed) {
      isBypassed = bypassed;
      effectEnabledState[activeEffect] = !bypassed;
      updateMasterBypassUI();
      const chk = document.getElementById("bypassSwitch");
      if (chk) chk.checked = !bypassed;
      const lbl = document.getElementById("stageSwitchLabel");
      if (lbl) {
        lbl.textContent = bypassed ? "OFF" : "ON";
        lbl.style.color = bypassed ? "var(--text-muted)" : "var(--accent-emerald-soft)";
      }
      const stageParams = document.getElementById("stageParametersContainer");
      if (stageParams) {
        stageParams.style.opacity = bypassed ? "0.45" : "1.0";
      }
      updateRackTilesUI();
      const diagBypass = document.getElementById("diagBypassState");
      if (diagBypass) diagBypass.textContent = bypassed ? "ON (BYPASSED)" : "OFF (ACTIVE)";
    }

    // --- Single Kombi-Button Play / Stop ---
    function togglePlayStopKombi() {
      if (isStreaming && streamType === "PLAYING") {
        stopAudio();
      } else {
        const trackSelect = document.getElementById("trackSelect");
        const val = trackSelect ? trackSelect.value : "";
        const idx = parseInt(val, 10);
        if (!val || isNaN(idx) || idx <= 0) {
          logTerminal("[WARN] No WAV track available or selected. Click Scan Tracks first.", "warn");
          return;
        }
        currentTrackIndex = idx;
        const matched = trackList.find(t => t.index === idx);
        if (matched) currentTrackName = matched.name;
        sendSerialCommand(`play ${idx}`);
        setStreamingState("PLAYING", currentTrackName);
      }
    }

    function stopAudio() {
      sendSerialCommand("stop");
      setStreamingState("STOPPED");
    }

    // --- Single Kombi-Button Live Microphone ---
    function toggleLiveMicKombi() {
      if (isStreaming && streamType === "LIVE_MIC") {
        stopAudio();
      } else {
        sendSerialCommand("audio start");
        setStreamingState("LIVE MIC", currentMic.toUpperCase());
      }
    }

    function setStreamingState(state, info = "") {
      const playBtn = document.getElementById("playBtn");
      const liveMicBtn = document.getElementById("liveMicBtn");
      const micSelect = document.getElementById("micSelect");
      const tLabel = document.getElementById("transportStateLabel");
      const mLabel = document.getElementById("micStateLabel");

      if (state === "STOPPED") {
        isStreaming = false;
        streamType = "STOPPED";
        if (playBtn) {
          playBtn.classList.remove("danger-active");
          playBtn.dataset.tooltip = "Play Selected Track (play <idx>)";
          playBtn.innerHTML = `
            <svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor">
              <polygon points="5 3 19 12 5 21 5 3"></polygon>
            </svg>`;
        }
        if (liveMicBtn) {
          liveMicBtn.classList.remove("active");
          liveMicBtn.dataset.tooltip = "Start Live Microphone (audio start)";
        }
        if (micSelect) micSelect.disabled = false;
        updateScopeMeta(isConnected ? "Hardware Ready · 48.0 kHz · Stereo" : "Standby · Offline");
        if (tLabel) {
          tLabel.textContent = isConnected ? "Hardware Ready" : "Standby (Offline)";
          tLabel.style.color = "var(--text-main)";
        }
        if (mLabel) {
          mLabel.textContent = isConnected ? "Standby" : "Offline";
          mLabel.style.color = "var(--text-muted)";
        }
        updateMeter("meterIn", "rmsInVal", -90);
        updateMeter("meterOut", "rmsOutVal", -90);
      } else if (state === "PLAYING") {
        isStreaming = true;
        streamType = "PLAYING";
        if (playBtn) {
          playBtn.classList.add("danger-active");
          playBtn.dataset.tooltip = "Stop Audio Playback (stop)";
          playBtn.innerHTML = `
            <svg width="12" height="12" viewBox="0 0 24 24" fill="currentColor">
              <rect x="5" y="5" width="14" height="14"></rect>
            </svg>`;
        }
        if (liveMicBtn) liveMicBtn.classList.remove("active");
        if (micSelect) micSelect.disabled = true;
        const trackTitle = info || currentTrackName || (trackList.find(t => t.index === currentTrackIndex)?.name) || `Track #${currentTrackIndex}`;
        updateScopeMeta(`Playing: ${trackTitle} · 48.0 kHz · Stereo`);
        if (tLabel) {
          tLabel.textContent = "Playing Track";
          tLabel.style.color = "var(--accent-emerald-soft)";
        }
        if (mLabel) {
          mLabel.textContent = "Standby (WAV Active)";
          mLabel.style.color = "var(--text-muted)";
        }
      } else if (state === "LIVE MIC") {
        isStreaming = true;
        streamType = "LIVE_MIC";
        if (liveMicBtn) {
          liveMicBtn.classList.add("active");
          liveMicBtn.dataset.tooltip = "Stop Live Microphone Loop (stop)";
        }
        if (playBtn) {
          playBtn.classList.remove("danger-active");
          playBtn.dataset.tooltip = "Play Selected Track (play <idx>)";
          playBtn.innerHTML = `
            <svg width="14" height="14" viewBox="0 0 24 24" fill="currentColor">
              <polygon points="5 3 19 12 5 21 5 3"></polygon>
            </svg>`;
        }
        if (micSelect) micSelect.disabled = true;
        const micLabel = info || currentMic.toUpperCase();
        updateScopeMeta(`Live Mic (${micLabel}) · 48.0 kHz · Stereo`);
        if (tLabel) {
          tLabel.textContent = "Live Mic Streaming";
          tLabel.style.color = "var(--accent-ice)";
        }
        if (mLabel) {
          mLabel.textContent = "Live Stream Active";
          mLabel.style.color = "var(--accent-ice)";
        }
      }
    }

    // --- Volume & Mic Gain ---
    let volDebounce = null;
    function onVolumeInput(val) {
      const v = parseFloat(val);
      lastVolume = v;
      isMuted = (v <= -59.5);
      const vVal = document.getElementById("volumeVal");
      if (vVal) vVal.textContent = `${v.toFixed(1)} dB`;
      const btn = document.getElementById("btnMuteVol");
      if (btn) {
        btn.classList.toggle("danger-active", isMuted);
        btn.dataset.tooltip = isMuted ? "Unmute Speaker" : "Mute Speaker";
      }
      if (volDebounce) clearTimeout(volDebounce);
      volDebounce = setTimeout(() => {
        sendSerialCommand(`volume ${v.toFixed(1)}`);
      }, 100);
    }

    function toggleMuteVolume() {
      const slider = document.getElementById("volumeSlider");
      if (!slider) return;
      if (!isMuted) {
        lastVolume = parseFloat(slider.value);
        slider.value = -60;
        onVolumeInput(-60);
      } else {
        const restore = lastVolume > -59 ? lastVolume : -25;
        slider.value = restore;
        onVolumeInput(restore);
      }
    }

    let micGainDebounce = null;
    function onMicGainInput(val) {
      const g = parseFloat(val);
      document.getElementById("micGainVal").textContent = `${g.toFixed(1)} dB`;
      if (micGainDebounce) clearTimeout(micGainDebounce);
      micGainDebounce = setTimeout(() => {
        sendSerialCommand(`mic_gain ${g.toFixed(1)}`);
      }, 100);
    }

    function applyMicSelection(mic) {
      currentMic = mic;
      const sel = document.getElementById("micSelect");
      if (sel) sel.value = mic;
      const group = document.getElementById("micGainGroup");
      const isCodec = (mic === "codec");
      if (group) group.style.opacity = isCodec ? "1.0" : "0.35";
      const slider = document.getElementById("micGainSlider");
      if (slider) slider.disabled = !isCodec;
      const hwLabel = document.getElementById("micHardwareLabel");
      const preampLabel = document.getElementById("micPreampLabel");
      if (hwLabel) {
        hwLabel.textContent = isCodec ? "ES8311 Codec ADC" : "I2S1 Digital MEMS";
      }
      if (preampLabel) {
        preampLabel.textContent = isCodec ? "Analog PGA (0-42 dB)" : "Fixed 24-bit I2S";
      }
      if (isStreaming && streamType === "LIVE_MIC") {
        updateScopeMeta(`Live Mic (${mic.toUpperCase()}) · 48.0 kHz · Stereo`);
      }
    }

    function onMicSelectChanged() {
      const mic = document.getElementById("micSelect").value;
      if (isStreaming) {
        logTerminal("[WARN] Audio stream is running. Stop stream before switching microphone hardware.", "warn");
        const sel = document.getElementById("micSelect");
        if (sel) sel.value = currentMic;
        return;
      }
      applyMicSelection(mic);
      sendSerialCommand(`mic_select ${mic}`);
    }
