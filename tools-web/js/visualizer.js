"use strict";

// --- Oscilloscope Scope Animation ---
    let scopePaused = false;
    
    window.resetScope = function() {
      // Reset phase for scope animation
      const canvas = document.getElementById("scopeCanvas");
      if (canvas) {
        const ctx = canvas.getContext("2d");
        ctx.fillStyle = "#05070a";
        ctx.fillRect(0, 0, canvas.width, canvas.height);
      }
    };
    
    window.togglePauseScope = function() {
      scopePaused = !scopePaused;
      const btn = document.getElementById('btnPauseScope');
      if (btn) {
        if (scopePaused) {
          btn.classList.add('active');
          btn.setAttribute('data-tooltip', 'Resume Signal Capture');
        } else {
          btn.classList.remove('active');
          btn.setAttribute('data-tooltip', 'Pause Signal Capture');
        }
      }
    };
    
    function initScopeVisualizer() {
      const canvas = document.getElementById("scopeCanvas");
      if (!canvas) return;
      const ctx = canvas.getContext("2d");
      let phase = 0;

      function render() {
        ctx.fillStyle = "#05070a";
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        // Center line
        ctx.strokeStyle = "rgba(255, 255, 255, 0.08)";
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(0, canvas.height / 2);
        ctx.lineTo(canvas.width, canvas.height / 2);
        ctx.stroke();

        if (isStreaming && !scopePaused) {
          ctx.strokeStyle = isBypassed ? "#f59e0b" : "#38bdf8";
          ctx.lineWidth = 2;
          ctx.shadowBlur = 8;
          ctx.shadowColor = isBypassed ? "rgba(245, 158, 11, 0.4)" : "rgba(56, 189, 248, 0.4)";
          ctx.beginPath();

          const amp = isBypassed ? 15 : 28;
          const freq = activeEffect === "bitcrusher" ? 0.03 : 0.045;

          for (let x = 0; x < canvas.width; x++) {
            let y = canvas.height / 2 + Math.sin(x * freq + phase) * amp;
            if (activeEffect === "bitcrusher") {
              // Quantized step appearance
              y = Math.round(y / 8) * 8;
            }
            if (x === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
          }
          ctx.stroke();
          ctx.shadowBlur = 0;
          phase += 0.12;
        }

        requestAnimationFrame(render);
      }
      requestAnimationFrame(render);
    }
