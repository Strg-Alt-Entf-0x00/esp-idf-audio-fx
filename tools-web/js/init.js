"use strict";

// --- Boot Initializer ---
    document.addEventListener("DOMContentLoaded", () => {
      buildEffectGrid();
      selectEffect("reverb", false);
      initScopeVisualizer();
      logTerminal("[SYS] System ready. Connect USB or use controls for live preview.", "info");
    });
