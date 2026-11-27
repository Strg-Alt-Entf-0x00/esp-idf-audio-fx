"use strict";

// --- Audio Source Mode Switching (WAV Flash vs Live Mic) ---
    function setAudioSourceMode(mode, fromRemote = false) {
      if (!fromRemote && isStreaming && currentSourceMode !== mode) {
        logTerminal("[WARN] Audio stream active. Stopping playback before switching source.", "warn");
        stopAudio();
      }
      currentSourceMode = mode;
      const btnWav = document.getElementById("btnSourceWav");
      const btnMic = document.getElementById("btnSourceMic");
      const secWav = document.getElementById("sourceSectionWav");
      const secMic = document.getElementById("sourceSectionMic");
      if (btnWav) btnWav.classList.toggle("active", mode === "wav");
      if (btnMic) btnMic.classList.toggle("active", mode === "mic");
      if (secWav) secWav.style.display = (mode === "wav") ? "flex" : "none";
      if (secMic) secMic.style.display = (mode === "mic") ? "flex" : "none";
    }

    // --- Tab Switching (REMOVED - No longer needed without tabs) ---
    // function switchTab(tabId) {
    //   document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
    //   document.querySelectorAll('.tab-body').forEach(t => t.style.display = 'none');
    //   const target = document.getElementById(tabId);
    //   if (target) target.style.display = 'flex';
    //   const btns = Array.from(document.querySelectorAll('.tab-btn'));
    //   const activeBtn = btns.find(b => b.getAttribute('onclick')?.includes(tabId));
    //   if (activeBtn) activeBtn.classList.add('active');
    // }

    // --- Drawer & Modal Logic ---
    function toggleDrawer(open) {
      const drawer = document.getElementById('bottomTerminalDrawer');
      if (open === undefined) drawer.classList.toggle('open');
      else drawer.classList.toggle('open', open);
    }

    document.getElementById('btnToggleDrawer').addEventListener('click', () => toggleDrawer());
    document.getElementById('btnCopyLog').addEventListener('click', async () => {
      try {
        await copyTextToClipboard(readTerminalLog());
      } catch (err) {
        logTerminal(`Could not copy serial log: ${err.message}`, 'err');
      }
    });
    document.getElementById('btnDownloadLog').addEventListener('click', downloadTerminalLog);

    function readTerminalLog() {
      const box = document.getElementById('terminalLog');
      return Array.from(box.children, line => line.textContent).join('\n');
    }

    async function copyTextToClipboard(text) {
      if (navigator.clipboard && window.isSecureContext) {
        await navigator.clipboard.writeText(text);
        return;
      }

      const textarea = document.createElement('textarea');
      textarea.value = text;
      textarea.setAttribute('readonly', '');
      textarea.style.position = 'fixed';
      textarea.style.opacity = '0';
      document.body.appendChild(textarea);
      let copied;
      try {
        textarea.select();
        copied = document.execCommand('copy');
      } finally {
        textarea.remove();
      }
      if (!copied) throw new Error('Clipboard access is unavailable in this browser.');
    }

    function downloadTerminalLog() {
      let objectUrl;
      try {
        const blob = new Blob([readTerminalLog()], { type: 'text/plain;charset=utf-8' });
        objectUrl = URL.createObjectURL(blob);
        const link = document.createElement('a');
        link.href = objectUrl;
        link.download = 'audio-fx-serial-console-log.txt';
        document.body.appendChild(link);
        link.click();
        link.remove();
      } catch (err) {
        logTerminal(`Could not download serial log: ${err.message}`, 'err');
      } finally {
        if (objectUrl) window.setTimeout(() => URL.revokeObjectURL(objectUrl), 1000);
      }
    }

    const helpModal = document.getElementById('helpModal');
    document.getElementById('btnHelp').addEventListener('click', () => helpModal.classList.add('open'));
    document.getElementById('btnCloseHelp').addEventListener('click', () => helpModal.classList.remove('open'));
    helpModal.addEventListener('click', (e) => { if (e.target === helpModal) helpModal.classList.remove('open'); });
    window.addEventListener('keydown', (e) => {
      if (e.key === 'Escape') {
        helpModal.classList.remove('open');
        toggleDrawer(false);
      }
    });

    function updateRackTilesUI() {
      for (const key of Object.keys(EFFECTS_METADATA)) {
        const tile = document.getElementById(`fx_tile_${key}`);
        if (!tile) continue;
        const isSel = (key === activeEffect);
        const isEn = Boolean(effectEnabledState[key]);
        tile.classList.toggle("selected", isSel);
        tile.classList.toggle("fx-active", isEn);
      }
    }

    // --- Build Effect Rack UI ---
    function buildEffectGrid() {
      const grid = document.getElementById("effectGrid");
      grid.innerHTML = "";
      for (const [key, fx] of Object.entries(EFFECTS_METADATA)) {
        const tile = document.createElement("div");
        const isSel = (key === activeEffect);
        const isEn = Boolean(effectEnabledState[key]);
        tile.className = `effect-tile ${isSel ? "selected" : ""} ${isEn ? "fx-active" : ""}`;
        tile.id = `fx_tile_${key}`;
        tile.onclick = () => selectEffect(key, true);
        tile.innerHTML = `
          <span class="tile-name">${fx.name}</span>
          <span class="tile-desc">${fx.desc}</span>
        `;
        grid.appendChild(tile);
      }
    }

    function selectEffect(effectKey, sendCommand = true) {
      if (!EFFECTS_METADATA[effectKey]) return;
      activeEffect = effectKey;

      updateRackTilesUI();

      const fx = EFFECTS_METADATA[effectKey];
      const titleEl = document.getElementById("stageEffectName");
      if (titleEl) titleEl.textContent = fx.name.toUpperCase();
      const diagSlot = document.getElementById("diagActiveEffect");
      if (diagSlot) diagSlot.textContent = fx.name.toUpperCase();

      // Reflect THIS effect's individual enabled state in the switch
      const isEn = Boolean(effectEnabledState[effectKey]);
      isBypassed = !isEn;
      updateMasterBypassUI();
      const chk = document.getElementById("bypassSwitch");
      if (chk) chk.checked = isEn;
      const lbl = document.getElementById("stageSwitchLabel");
      if (lbl) {
        lbl.textContent = isEn ? "ON" : "OFF";
        lbl.style.color = isEn ? "var(--accent-emerald-soft)" : "var(--text-muted)";
      }
      const stageParams = document.getElementById("stageParametersContainer");
      if (stageParams) {
        stageParams.style.opacity = isEn ? "1.0" : "0.45";
      }

      renderParameters(effectKey);

      if (sendCommand) {
        sendSerialCommand(`fx select ${effectKey}`);
        sendSerialCommand(`fx bypass ${isEn ? "off" : "on"}`);
        setTimeout(() => sendSerialCommand("fx params"), 120);
      }
    }

    function renderParameters(effectKey) {
      const container = document.getElementById("stageParametersContainer");
      container.innerHTML = "";
      const fx = EFFECTS_METADATA[effectKey];

      if (fx.isEq) {
        renderEqVisualizer(container, fx);
        return;
      }

      fx.params.forEach(param => {
        const card = document.createElement("div");
        card.className = "param-card";
        card.innerHTML = `
          <div class="param-card-head">
            <span class="param-name-label">${param.name.replace(/_/g, " ")}</span>
            <span class="param-val-badge" id="val_label_${param.index}">${param.default}${param.unit}</span>
          </div>
          <div>
            <input type="range" id="param_slider_${param.index}"
              min="${param.min}" max="${param.max}" step="${param.step}" value="${param.default}"
              oninput="onParamSliderInput(${param.index}, this.value, '${param.unit}')">
          </div>
        `;
        container.appendChild(card);
      });
    }

    function renderEqVisualizer(container, fx) {
      const eqBox = document.createElement("div");
      eqBox.className = "eq-container";

      const bands = [
        { name: "80 Hz", gainIdx: 1, freqIdx: 0, qIdx: 2 },
        { name: "250 Hz", gainIdx: 4, freqIdx: 3, qIdx: 5 },
        { name: "1 kHz", gainIdx: 7, freqIdx: 6, qIdx: 8 },
        { name: "4 kHz", gainIdx: 10, freqIdx: 9, qIdx: 11 },
        { name: "12 kHz", gainIdx: 13, freqIdx: 12, qIdx: 14 }
      ];

      bands.forEach(b => {
        const paramDef = fx.params.find(p => p.index === b.gainIdx);
        const defaultVal = paramDef ? paramDef.default : 0;
        const sign = defaultVal > 0 ? "+" : "";
        const bandEl = document.createElement("div");
        bandEl.className = "eq-band";
        bandEl.innerHTML = `
          <span class="param-val-badge" id="val_label_${b.gainIdx}" style="font-size: 11px;">${sign}${defaultVal.toFixed(1)} dB</span>
          <input type="range" class="eq-slider-vert" id="param_slider_${b.gainIdx}"
            orient="vertical" min="-12" max="12" step="0.5" value="${defaultVal}"
            oninput="onParamSliderInput(${b.gainIdx}, this.value, ' dB')">
          <span class="eq-band-label">${b.name}</span>
        `;
        eqBox.appendChild(bandEl);
      });

      container.appendChild(eqBox);
    }

    let paramDebounceTimer = null;
    function onParamSliderInput(index, value, unit) {
      const label = document.getElementById(`val_label_${index}`);
      if (label) {
        const v = parseFloat(value);
        if (unit.trim() === "dB") {
          const sign = v > 0 ? "+" : "";
          label.textContent = `${sign}${v.toFixed(1)}${unit}`;
        } else {
          label.textContent = `${v.toFixed(2)}${unit}`;
        }
      }

      if (paramDebounceTimer) clearTimeout(paramDebounceTimer);
      paramDebounceTimer = setTimeout(() => {
        sendSerialCommand(`fx set ${index} ${parseFloat(value).toFixed(6)}`);
      }, 100);
    }

    function resetCurrentEffectDefaults() {
      const fx = EFFECTS_METADATA[activeEffect];
      if (!fx) return;
      fx.params.forEach(p => {
        const slider = document.getElementById(`param_slider_${p.index}`);
        if (slider) {
          slider.value = p.default;
          onParamSliderInput(p.index, p.default, p.unit);
        }
      });
      logTerminal(`[FX] Reset ${fx.name.toUpperCase()} to factory defaults`, "info");
    }
