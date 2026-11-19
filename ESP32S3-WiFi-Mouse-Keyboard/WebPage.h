#pragma once
#include <Arduino.h>

static const char MAIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<title>ESP32-S3 Air Input</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}

  /* ── Full-height page layout ── */
  html,body{height:100%;height:100dvh;overflow:hidden}
  body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Helvetica,Arial,sans-serif;background:#1a1a2e;color:#eee;display:flex;flex-direction:column}

  /* Scrollable area only for settings/keyboard tabs */
  .tab-content{display:none;overflow-y:auto;flex:1;padding:0 10px 12px}
  .tab-content.active{display:flex;flex-direction:column}

  /* Trackpad tab: fills viewport, no scroll */
  #tab-trackpad{overflow:hidden;padding:0}

  .app-header{display:flex;align-items:center;justify-content:space-between;max-width:540px;width:100%;margin:0 auto;padding:8px 12px 0;flex-shrink:0}
  .app-title{font-size:1.1em;font-weight:700;color:#e94560;display:flex;align-items:center;gap:8px}
  .status-pill{font-size:.72em;padding:3px 8px;border-radius:20px;font-weight:600;display:inline-flex;align-items:center;gap:5px}
  .pill-ok{background:#1b5e20;color:#a5d6a7}
  .pill-warn{background:#e65100;color:#ffe0b2}
  .pill-err{background:#b71c1c;color:#ef9a9a}
  .tabs{display:flex;gap:6px;max-width:540px;width:100%;margin:6px auto 0;background:#0d1b2a;padding:4px;border-radius:8px;flex-shrink:0;align-self:center}
  .tab-btn{flex:1;padding:8px;background:none;border:none;color:#aaa;border-radius:6px;font-size:.88em;font-weight:600;cursor:pointer;transition:all .15s}
  .tab-btn.active{background:#e94560;color:#fff}
  .card{background:#16213e;border-radius:12px;padding:14px;margin:0 auto 10px;max-width:540px;width:100%;box-shadow:0 4px 16px rgba(0,0,0,.35)}
  .card-header{display:flex;align-items:center;justify-content:space-between;margin-bottom:10px;padding-bottom:8px;border-bottom:1px solid #0f3460}
  .card-title{font-size:.95em;font-weight:700;color:#00adb5;display:flex;align-items:center;gap:6px}
  .tool-btn{background:#0f3460;color:#fff;border:none;border-radius:6px;padding:5px 10px;font-size:.78em;font-weight:600;cursor:pointer;display:inline-flex;align-items:center;gap:4px;transition:background .2s}
  .tool-btn:hover{background:#1a4d8f}
  .tool-btn.accent{background:#e94560}
  .tool-btn.accent:hover{background:#c73652}
  .tool-btn.active-jiggle{background:#1b5e20;color:#a5d6a7;border:1px solid #4caf50;box-shadow:0 0 8px rgba(76,175,80,.6)}
  .speed-select{background:#0f3460;color:#fff;border:none;border-radius:6px;padding:5px 7px;font-size:.78em;outline:none}

  /* ── Trackpad card: fills remaining height ── */
  #tp-wrapper{background:#16213e;border-radius:12px;padding:10px 12px;max-width:540px;width:100%;margin:6px auto 0;display:flex;flex-direction:column;flex:1;min-height:0;box-shadow:0 4px 16px rgba(0,0,0,.35)}

  /* trackpad + scroll strip row: grows to fill */
  #tp-row{display:flex;gap:8px;flex:1;min-height:0}
  #trackpad{flex:1;min-height:0;background:#0d1b2a;border:2px dashed #0f3460;border-radius:10px;position:relative;touch-action:none;user-select:none;-webkit-user-select:none;cursor:crosshair;display:flex;flex-direction:column;align-items:center;justify-content:center;overflow:hidden}
  #tp-label{font-size:.78em;color:#556b82;pointer-events:none;text-align:center;line-height:1.5}
  .touch-dot{position:absolute;width:26px;height:26px;border-radius:50%;background:rgba(233,69,96,.45);border:2px solid #e94560;pointer-events:none;transform:translate(-50%,-50%);transition:transform .05s,opacity .15s;opacity:0}

  /* ── Right-side Scroll Strip ── */
  #scroll-strip{width:34px;min-height:0;background:#0d1b2a;border:2px solid #0f3460;border-radius:10px;touch-action:none;user-select:none;-webkit-user-select:none;cursor:ns-resize;display:flex;align-items:center;justify-content:center;position:relative;flex-shrink:0}
  #scroll-strip::before{content:'';position:absolute;top:12px;bottom:12px;left:50%;transform:translateX(-50%);width:3px;background:#1a3a5c;border-radius:2px}
  #scroll-thumb{position:absolute;width:18px;height:38px;background:#e94560;border-radius:6px;left:50%;transform:translateX(-50%);top:50%;margin-top:-19px;transition:opacity .15s;opacity:.7}
  #scroll-strip:active #scroll-thumb,#scroll-strip.active #scroll-thumb{opacity:1}
  #scroll-strip-label{position:absolute;bottom:5px;font-size:.55em;color:#556b82;pointer-events:none;white-space:nowrap}

  /* ── Mouse Button + Keyboard strip at bottom ── */
  #tp-bottom{flex-shrink:0;display:flex;flex-direction:column;gap:7px;margin-top:8px}
  .mouse-btn-row{display:flex;gap:8px}
  .m-btn{flex:1;padding:13px 8px;background:#0f3460;border:none;border-radius:8px;color:#fff;font-size:.9em;font-weight:700;cursor:pointer;user-select:none;-webkit-user-select:none;transition:background .1s,transform .05s}
  .m-btn:active,.m-btn.down{background:#e94560;transform:scale(.97)}
  .m-btn.mid{background:#092540}

  /* Keyboard capture row */
  #kb-row{display:flex;align-items:center;gap:8px}
  #liveInput{flex:1;padding:9px 11px;border:1px solid #0f3460;border-radius:8px;background:#0d1b2a;color:#eee;font-size:.9em;outline:none;caret-color:transparent}
  #liveInput:focus{border-color:#e94560}
  #kb-open-btn{flex-shrink:0;padding:9px 12px;background:#e94560;border:none;border-radius:8px;color:#fff;font-size:.82em;font-weight:700;cursor:pointer;white-space:nowrap}

  /* ── Fullscreen Overlay Mode ── */
  #tp-wrapper.fullscreen{position:fixed;top:0;left:0;right:0;bottom:0;width:100vw;height:100vh;height:100dvh;z-index:99999;background:#0a0e17;border-radius:0;margin:0;padding:10px;max-width:none}
  #tp-wrapper.fullscreen #tp-row{flex:1}
  #tp-wrapper.fullscreen #trackpad{border:2px solid #e94560;border-radius:12px}
  #tp-wrapper.fullscreen #scroll-strip{border-color:#e94560}
  .fs-overlay-top{display:none;align-items:center;justify-content:space-between;margin-bottom:8px;flex-shrink:0}
  #tp-wrapper.fullscreen .fs-overlay-top{display:flex}
  .fs-overlay-bottom{display:none;gap:10px;margin-top:8px;flex-shrink:0}
  #tp-wrapper.fullscreen .fs-overlay-bottom{display:flex}
  .fs-overlay-bottom .m-btn{padding:20px 10px;font-size:1.05em}

  /* ── Keyboard & Settings tabs (scrollable) ── */
  .scroll-card-wrap{padding:10px 10px 16px;max-width:540px;width:100%;margin:0 auto}
  .input-box{width:100%;padding:11px;border:1px solid #0f3460;border-radius:6px;background:#0d1b2a;color:#eee;font-size:1em;outline:none}
  .input-box:focus{border-color:#e94560}
  .key-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(60px,1fr));gap:6px;margin-top:10px}
  .k-btn{padding:11px 6px;background:#0f3460;border:none;border-radius:6px;color:#fff;font-size:.85em;font-weight:600;cursor:pointer;user-select:none;text-align:center;transition:background .1s,transform .05s}
  .k-btn:active{background:#e94560;transform:scale(.95)}
  .k-btn.active{background:#e94560;box-shadow:0 0 10px rgba(233,69,96,.7)}
  .k-btn.wide{grid-column:span 2}
  .k-btn.action{background:#00adb5;color:#0d1b2a}
  label{display:block;margin:12px 0 4px;font-size:.85em;color:#aaa}
  .row{display:flex;gap:16px;margin:10px 0}
  .row label{display:flex;align-items:center;gap:6px;cursor:pointer;font-size:.95em;color:#ccc}
  .badge{display:inline-block;padding:4px 10px;border-radius:12px;font-size:.8em;font-weight:bold}
  .ok{background:#1b5e20;color:#a5d6a7}.err{background:#b71c1c;color:#ef9a9a}
  .sep{border:none;border-top:1px solid #0f3460;margin:14px 0}
  .save-btn{width:100%;padding:13px;background:#e94560;color:white;border:none;border-radius:8px;font-size:1em;font-weight:700;cursor:pointer;margin-top:10px;transition:background .2s}
  .save-btn:hover{background:#c73652}
  .info{font-size:.8em;color:#888;margin-top:6px}
</style>
</head>
<body>

<div class="app-header">
  <div class="app-title">🖱️ ESP32-S3 Air Input</div>
  <div id="ws-pill" class="status-pill pill-warn">⚡ Connecting...</div>
</div>

<div class="tabs">
  <button class="tab-btn active" onclick="switchTab('tab-trackpad')">🖱️ Trackpad</button>
  <button class="tab-btn" onclick="switchTab('tab-keyboard')">⌨️ Keys</button>
  <button class="tab-btn" onclick="switchTab('tab-settings')">⚙️ Settings</button>
</div>

<!-- ═════════════════════ TAB 1: TRACKPAD ═════════════════════ -->
<div id="tab-trackpad" class="tab-content active" style="display:flex;flex-direction:column">
  <div id="tp-wrapper">
    <!-- Top Bar -->
    <div class="card-header" id="tp-card-header">
      <div class="card-title">🎯 Trackpad</div>
      <div style="display:flex;align-items:center;gap:5px;flex-wrap:wrap">
        <button id="circleBtn" class="tool-btn" onclick="toggleCircleMotion()" title="Circle jiggle">🌀 OFF</button>
        <select id="speedSelect" class="speed-select" onchange="setSpeed(this.value)">
          <option value="0.75">0.75x</option>
          <option value="1.0">1.0x</option>
          <option value="1.5" selected>1.5x</option>
          <option value="2.0">2.0x</option>
          <option value="2.5">2.5x</option>
          <option value="3.0">3.0x</option>
        </select>
        <button class="tool-btn accent" onclick="toggleFullscreen()">⛶ FS</button>
      </div>
    </div>

    <!-- Fullscreen Floating Header -->
    <div class="fs-overlay-top">
      <button class="tool-btn accent" onclick="focusVirtualKeyboard()">⌨️ Keyboard</button>
      <button id="circleBtnFs" class="tool-btn" onclick="toggleCircleMotion()">🌀 Circle: OFF</button>
      <button class="tool-btn" onclick="toggleFullscreen()">✕ Exit</button>
    </div>

    <!-- Touch Surface + Scroll Strip -->
    <div id="tp-row">
      <div id="trackpad">
        <div class="touch-dot" id="touchDot"></div>
        <div id="tp-label">
          <b>Touch &amp; Drag</b>: Move &nbsp;|&nbsp; <b>Tap</b>: Click<br>
          <b>Double-Tap Hold</b>: Drag &nbsp;|&nbsp; <b>2-Finger</b>: Scroll
        </div>
      </div>
      <div id="scroll-strip">
        <div id="scroll-thumb"></div>
        <span id="scroll-strip-label">↕</span>
      </div>
    </div>

    <!-- Bottom: Middle + Right Click buttons + Keyboard capture row -->
    <div id="tp-bottom">
      <div class="mouse-btn-row">
        <button class="m-btn mid" id="btn-mid" onpointerdown="btnDown(3)" onpointerup="btnUp(3)">⬤ Middle</button>
        <button class="m-btn" id="btn-right" onpointerdown="btnDown(2)" onpointerup="btnUp(2)">Right Click ▶</button>
      </div>
      <div id="kb-row">
        <input type="search" inputmode="text" id="liveInput"
               placeholder="⌨️ Tap here to type on PC"
               autocomplete="off" autocorrect="off" autocapitalize="off"
               spellcheck="false" enterkeyhint="send"
               readonly tabindex="-1">
        <button id="kb-open-btn" onclick="focusVirtualKeyboard()">⌨️ Open</button>
      </div>
    </div>

    <!-- Fullscreen Bottom Buttons -->
    <div class="fs-overlay-bottom">
      <button class="m-btn mid" onpointerdown="btnDown(3)" onpointerup="btnUp(3)">⬤ MIDDLE</button>
      <button class="m-btn" onpointerdown="btnDown(2)" onpointerup="btnUp(2)">RIGHT CLICK ▶</button>
    </div>
  </div>
</div>

<!-- ═════════════════════ TAB 2: KEYBOARD ═════════════════════ -->
<div id="tab-keyboard" class="tab-content" style="display:none">
  <div class="scroll-card-wrap">

  <!-- Text Paste & Send -->
  <div class="card">
    <div class="card-header">
      <div class="card-title">🚀 Send Full Text / Command</div>
    </div>
    <textarea id="textPayload" class="input-box" rows="2" placeholder="Paste or type URL, command, or sentence to type out on PC..."></textarea>
    <button class="save-btn" onclick="sendFullText()" style="margin-top:10px">Type Text on PC</button>
  </div>

  <!-- Special Desktop Keys & Navigation -->
  <div class="card">
    <div class="card-header">
      <div class="card-title">⌨️ Special Keys &amp; Navigation</div>
    </div>
    <div class="key-grid">
      <button class="k-btn" onclick="sendSpecial(0x29)">Esc</button>
      <button class="k-btn" onclick="sendSpecial(0x2B)">Tab</button>
      <button class="k-btn" onclick="sendSpecial(0x2A)">⌫ Back</button>
      <button class="k-btn action" onclick="sendSpecial(0x28)">↵ Enter</button>
      <button class="k-btn" onclick="sendSpecial(0x4C)">Del</button>
      <button class="k-btn wide" onclick="sendSpecial(0x2C)">Space</button>
    </div>
    
    <label style="margin-top:14px">Navigation Keys</label>
    <div class="key-grid">
      <button class="k-btn" onclick="sendSpecial(0x52)">▲ Up</button>
      <button class="k-btn" onclick="sendSpecial(0x50)">◀ Left</button>
      <button class="k-btn" onclick="sendSpecial(0x51)">▼ Down</button>
      <button class="k-btn" onclick="sendSpecial(0x4F)">▶ Right</button>
      <button class="k-btn" onclick="sendSpecial(0x4A)">Home</button>
      <button class="k-btn" onclick="sendSpecial(0x4D)">End</button>
    </div>

    <label style="margin-top:14px">Modifier Toggles &amp; Combos</label>
    <div class="key-grid">
      <button class="k-btn" id="mod-ctrl" onclick="toggleMod(1, this)">Ctrl</button>
      <button class="k-btn" id="mod-shift" onclick="toggleMod(2, this)">Shift</button>
      <button class="k-btn" id="mod-alt" onclick="toggleMod(4, this)">Alt</button>
      <button class="k-btn" id="mod-gui" onclick="toggleMod(8, this)">Win/Cmd</button>
      <button class="k-btn" onclick="sendCombo(1, 0x06)">Ctrl+C</button>
      <button class="k-btn" onclick="sendCombo(1, 0x19)">Ctrl+V</button>
      <button class="k-btn" onclick="sendCombo(1, 0x1D)">Ctrl+Z</button>
      <button class="k-btn" onclick="sendCombo(4, 0x2B)">Alt+Tab</button>
    </div>
  </div>

  </div><!-- end scroll-card-wrap -->
</div>

<!-- ═════════════════════ TAB 3: SETTINGS ═════════════════════ -->
<div id="tab-settings" class="tab-content" style="display:none">
  <div class="scroll-card-wrap">
  <div class="card">
    <div class="card-header">
      <div class="card-title">📊 System &amp; Network Status</div>
    </div>
    <p>USB HID: <span class="badge ok">%USB_STATUS%</span></p>
    <p style="margin-top:8px">Mode: <span class="badge ok">%WIFI_MODE%</span></p>
    <p style="margin-top:8px">IP: <b>%WIFI_IP%</b> &nbsp;|&nbsp; Host: <b>http://%MDNS_HOST%.local</b></p>
    <p style="margin-top:8px">Connected Browsers: <b id="ws-client-count">1</b></p>
  </div>

  <form action="/save" method="POST">
  <div class="card">
    <div class="card-header">
      <div class="card-title">📡 WiFi Mode Settings</div>
    </div>
    <div class="row">
      <label><input type="radio" name="mode" value="ap" %AP_CHK%> Access Point (AP)</label>
      <label><input type="radio" name="mode" value="sta" %STA_CHK%> Station (join router)</label>
    </div>
    <hr class="sep">
    <b style="color:#e94560;font-size:.85em">ACCESS POINT</b>
    <label>SSID</label>
    <input type="text" name="ap_ssid" class="input-box" value="%AP_SSID%" maxlength="32">
    <label>Password <span class="info">(min 8 characters)</span></label>
    <input type="password" name="ap_pass" class="input-box" value="%AP_PASS%" maxlength="64">
    <hr class="sep">
    <b style="color:#e94560;font-size:.85em">STATION (ROUTER)</b>
    <label>SSID</label>
    <input type="text" name="sta_ssid" class="input-box" value="%STA_SSID%" maxlength="32">
    <label>Password</label>
    <input type="password" name="sta_pass" class="input-box" value="%STA_PASS%" maxlength="64">
    <hr class="sep">
    <label>Device Hostname <span class="info">(.local mDNS hostname)</span></label>
    <input type="text" name="mdns" class="input-box" value="%MDNS_HOST%" maxlength="32" pattern="[a-zA-Z0-9\-]+" title="Letters, numbers and hyphens only">
    <p class="info" style="margin-top:6px">🌐 Open in browser: <b>http://%MDNS_HOST%.local</b></p>
  </div>
  <div style="max-width:540px;margin:0 auto">
    <button type="submit" class="save-btn">💾 Save &amp; Restart</button>
  </div>
  </form>
  </div><!-- end scroll-card-wrap -->
</div>

<script>
// ────────────────── WebSocket Connection ──────────────────
var ws = null;
var wsConnected = false;
var activeModifiers = 0;
var speedMultiplier = parseFloat(localStorage.getItem('mouse_speed') || '1.5');
document.getElementById('speedSelect').value = speedMultiplier.toFixed(1);

function connectWS() {
  var host = window.location.hostname || '192.168.4.1';
  var wsUrl = 'ws://' + host + ':81/';
  ws = new WebSocket(wsUrl);

  ws.onopen = function() {
    wsConnected = true;
    var pill = document.getElementById('ws-pill');
    pill.className = 'status-pill pill-ok';
    pill.innerHTML = '⚡ Connected';
  };

  ws.onclose = function() {
    wsConnected = false;
    var pill = document.getElementById('ws-pill');
    pill.className = 'status-pill pill-err';
    pill.innerHTML = '✗ Reconnecting...';
    setTimeout(connectWS, 1500);
  };

  ws.onerror = function() {
    ws.close();
  };

  ws.onmessage = function(e) {
    if (e.data.startsWith('CLIENTS:')) {
      document.getElementById('ws-client-count').innerText = e.data.substring(8);
    } else if (e.data.startsWith('JIGGLER:')) {
      var state = (e.data.substring(8) === '1');
      updateCircleUI(state);
    }
  };
}
connectWS();

function wsSend(msg) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(msg);
  }
}

// ────────────────── Circle Motion (20px) ──────────────────
var circleActive = false;

function toggleCircleMotion() {
  circleActive = !circleActive;
  wsSend('J:' + (circleActive ? '1' : '0'));
  updateCircleUI(circleActive);
}

function updateCircleUI(active) {
  circleActive = active;
  var b1 = document.getElementById('circleBtn');
  var b2 = document.getElementById('circleBtnFs');
  if (b1) {
    b1.innerText = active ? '🌀 ON' : '🌀 OFF';
    if (active) b1.classList.add('active-jiggle'); else b1.classList.remove('active-jiggle');
  }
  if (b2) {
    b2.innerText = active ? '🌀 Circle: ON' : '🌀 Circle: OFF';
    if (active) b2.classList.add('active-jiggle'); else b2.classList.remove('active-jiggle');
  }
}

// ────────────────── Tab Switching ──────────────────
function switchTab(tabId) {
  document.querySelectorAll('.tab-content').forEach(function(el) {
    el.style.display = 'none';
    el.classList.remove('active');
  });
  document.querySelectorAll('.tab-btn').forEach(function(el) { el.classList.remove('active'); });
  var tab = document.getElementById(tabId);
  // Trackpad tab uses flex layout; others scroll as block
  tab.style.display = (tabId === 'tab-trackpad') ? 'flex' : 'block';
  tab.classList.add('active');
  var btns = document.querySelectorAll('.tab-btn');
  if (tabId === 'tab-trackpad') btns[0].classList.add('active');
  if (tabId === 'tab-keyboard') btns[1].classList.add('active');
  if (tabId === 'tab-settings') btns[2].classList.add('active');
}

// ────────────────── visualViewport: shrink trackpad when soft keyboard opens ──
// Keeps #tp-bottom (middle button + keyboard input) always visible above IME
(function() {
  if (!window.visualViewport) return;
  function onViewport() {
    var vvH = window.visualViewport.height;
    var fullH = window.innerHeight;
    var kbH = Math.max(0, fullH - vvH - window.visualViewport.offsetTop);
    var tpWrapper = document.getElementById('tp-wrapper');
    if (!tpWrapper) return;
    // Shrink tp-wrapper by the keyboard intrusion height so bottom stays visible
    if (kbH > 50) {
      tpWrapper.style.height = (vvH
        - document.querySelector('.app-header').offsetHeight
        - document.querySelector('.tabs').offsetHeight
        - 12) + 'px';
    } else {
      tpWrapper.style.height = '';
    }
  }
  window.visualViewport.addEventListener('resize', onViewport);
  window.visualViewport.addEventListener('scroll', onViewport);
})();

function setSpeed(val) {
  speedMultiplier = parseFloat(val);
  localStorage.setItem('mouse_speed', val);
}

// ────────────────── Fullscreen API ──────────────────
function toggleFullscreen() {
  var wrapper = document.getElementById('tp-wrapper');
  var isFs = wrapper.classList.contains('fullscreen') || document.fullscreenElement || document.webkitFullscreenElement;
  
  if (!isFs) {
    if (wrapper.requestFullscreen) {
      wrapper.requestFullscreen().catch(function(){ wrapper.classList.add('fullscreen'); });
    } else if (wrapper.webkitRequestFullscreen) {
      wrapper.webkitRequestFullscreen();
    } else {
      wrapper.classList.add('fullscreen');
    }
  } else {
    if (document.exitFullscreen) {
      document.exitFullscreen().catch(function(){ wrapper.classList.remove('fullscreen'); });
    } else if (document.webkitExitFullscreen) {
      document.webkitExitFullscreen();
    }
    wrapper.classList.remove('fullscreen');
  }
}

document.addEventListener('fullscreenchange', function() {
  var wrapper = document.getElementById('tp-wrapper');
  if (document.fullscreenElement) {
    wrapper.classList.add('fullscreen');
  } else {
    wrapper.classList.remove('fullscreen');
  }
});
document.addEventListener('webkitfullscreenchange', function() {
  var wrapper = document.getElementById('tp-wrapper');
  if (document.webkitFullscreenElement) {
    wrapper.classList.add('fullscreen');
  } else {
    wrapper.classList.remove('fullscreen');
  }
});

// ────────────────── Trackpad Touch Engine ──────────────────
var tp = document.getElementById('trackpad');
var touchDot = document.getElementById('touchDot');
var lastX = 0, lastY = 0;
var touchStartTime = 0;
var touchStartX = 0, touchStartY = 0;
var totalMoveDist = 0;
var lastTapTime = 0;
var isDragging = false;
var lastScrollY = 0;
var lastScrollX = 0;      // For 2-finger horizontal scroll

tp.addEventListener('touchstart', function(e) {
  e.preventDefault();
  var touches = e.touches;
  var now = Date.now();

  if (touches.length === 1) {
    var t = touches[0];
    var rect = tp.getBoundingClientRect();
    lastX = t.clientX;
    lastY = t.clientY;
    touchStartX = t.clientX;
    touchStartY = t.clientY;
    touchStartTime = now;
    totalMoveDist = 0;

    // Check for double-tap drag
    if (now - lastTapTime < 280) {
      isDragging = true;
      wsSend('D:1'); // Mouse Left Down
    }

    // Touch dot feedback
    touchDot.style.left = (t.clientX - rect.left) + 'px';
    touchDot.style.top = (t.clientY - rect.top) + 'px';
    touchDot.style.opacity = '1';
  } else if (touches.length === 2) {
    // 2-finger scroll initial position
    lastScrollY = (touches[0].clientY + touches[1].clientY) / 2;
    lastScrollX = (touches[0].clientX + touches[1].clientX) / 2;
  }
}, {passive:false});

tp.addEventListener('touchmove', function(e) {
  e.preventDefault();
  var touches = e.touches;

  if (touches.length === 1) {
    var t = touches[0];
    var dx = t.clientX - lastX;
    var dy = t.clientY - lastY;
    lastX = t.clientX;
    lastY = t.clientY;

    totalMoveDist += Math.abs(dx) + Math.abs(dy);

    // Apply sensitivity & slight non-linear acceleration curve
    var speed = Math.sqrt(dx * dx + dy * dy);
    var accel = 1.0;
    if (speed > 8)  accel = 1.35;
    if (speed > 20) accel = 1.85;

    var finalDx = Math.round(dx * speedMultiplier * accel);
    var finalDy = Math.round(dy * speedMultiplier * accel);

    if (finalDx !== 0 || finalDy !== 0) {
      wsSend('M:' + finalDx + ',' + finalDy);
    }

    var rect = tp.getBoundingClientRect();
    touchDot.style.left = (t.clientX - rect.left) + 'px';
    touchDot.style.top = (t.clientY - rect.top) + 'px';
  } else if (touches.length === 2) {
    // 2-finger scroll (vertical & horizontal)
    var currentScrollY = (touches[0].clientY + touches[1].clientY) / 2;
    var currentScrollX = (touches[0].clientX + touches[1].clientX) / 2;
    var sdy = currentScrollY - lastScrollY;
    var sdx = currentScrollX - lastScrollX;
    if (Math.abs(sdy) >= 8) {
      var wheelStep = sdy > 0 ? 1 : -1;
      wsSend('W:' + wheelStep);
      lastScrollY = currentScrollY;
    }
    if (Math.abs(sdx) >= 8) {
      var hStep = sdx > 0 ? 1 : -1;
      wsSend('X:' + hStep);
      lastScrollX = currentScrollX;
    }
  }
}, {passive:false});

tp.addEventListener('touchend', function(e) {
  e.preventDefault();
  var now = Date.now();
  touchDot.style.opacity = '0';

  if (isDragging) {
    isDragging = false;
    wsSend('U:1'); // Mouse Left Up
    lastTapTime = 0;
    return;
  }

  // Detect 1-finger tap (movement < 8px, duration < 240ms) → Left Click
  if (totalMoveDist < 8 && (now - touchStartTime < 240)) {
    if (e.touches.length === 0 && e.changedTouches.length === 1) {
      wsSend('C:1');
      lastTapTime = now;
    }
  }
}, {passive:false});

tp.addEventListener('touchcancel', function() {
  touchDot.style.opacity = '0';
  if (isDragging) {
    isDragging = false;
    wsSend('U:1');
  }
});

// ────────────────── Right-Side Scroll Strip ──────────────────
var scrollStrip = document.getElementById('scroll-strip');
var scrollThumb = document.getElementById('scroll-thumb');
var stripLastY = 0;
var stripActive = false;
var stripAccum = 0;          // sub-pixel accumulator
var STRIP_THRESHOLD = 10;    // px per scroll tick

scrollStrip.addEventListener('touchstart', function(e) {
  e.preventDefault();
  if (e.touches.length === 1) {
    stripLastY = e.touches[0].clientY;
    stripAccum = 0;
    stripActive = true;
    scrollStrip.classList.add('active');
  }
}, {passive:false});

scrollStrip.addEventListener('touchmove', function(e) {
  e.preventDefault();
  if (!stripActive || e.touches.length !== 1) return;
  var dy = e.touches[0].clientY - stripLastY;
  stripLastY = e.touches[0].clientY;
  stripAccum += dy;

  // Update thumb visual position
  var rect = scrollStrip.getBoundingClientRect();
  var clampedY = Math.max(20, Math.min(rect.height - 20,
    e.touches[0].clientY - rect.top));
  scrollThumb.style.top = clampedY + 'px';
  scrollThumb.style.marginTop = '-20px';

  while (Math.abs(stripAccum) >= STRIP_THRESHOLD) {
    var step = stripAccum > 0 ? 1 : -1;
    wsSend('W:' + step);
    stripAccum -= step * STRIP_THRESHOLD;
  }
}, {passive:false});

scrollStrip.addEventListener('touchend', function(e) {
  e.preventDefault();
  stripActive = false;
  stripAccum = 0;
  scrollStrip.classList.remove('active');
  // Reset thumb to center
  scrollThumb.style.top = '50%';
  scrollThumb.style.marginTop = '-20px';
}, {passive:false});

scrollStrip.addEventListener('touchcancel', function() {
  stripActive = false;
  scrollStrip.classList.remove('active');
  scrollThumb.style.top = '50%';
  scrollThumb.style.marginTop = '-20px';
});

// ────────────────── Mouse Buttons ──────────────────
function btnDown(b) {
  wsSend('D:' + b);
}
function btnUp(b) {
  wsSend('U:' + b);
}

// ────────────────── Virtual Keyboard Input ──────────────────
// Android problem map:
//   • keydown fires with key='Unidentified' for most keys on GBoard/SwiftKey
//   • Enter, Backspace, Arrow keys DO fire correctly in keydown
//   • Ctrl / Meta / Esc are swallowed by Android before reaching WebView
//   • The 'input' event is the only reliable source for printable chars
//   • compositionstart/update/end handles CJK and autocorrect word-by-word

var liveInput = document.getElementById('liveInput');
var kbActive = false;          // true while liveInput has focus
var composing = false;         // true during IME composition (CJK etc.)

// HID usage codes for every key we handle in keydown
var KEY_HID = {
  'Enter':       0x28,
  'Backspace':   0x2A,
  'Escape':      0x29,
  'Tab':         0x2B,
  'Delete':      0x4C,
  'ArrowUp':     0x52,
  'ArrowDown':   0x51,
  'ArrowLeft':   0x50,
  'ArrowRight':  0x4F,
  'Home':        0x4A,
  'End':         0x4D,
  'PageUp':      0x4B,
  'PageDown':    0x4E,
  'F1':0x3A,'F2':0x3B,'F3':0x3C,'F4':0x3D,'F5':0x3E,'F6':0x3F,
  'F7':0x40,'F8':0x41,'F9':0x42,'F10':0x43,'F11':0x44,'F12':0x45,
  ' ':           0x2C,
  'CapsLock':    0x39,
  'Insert':      0x49,
};

// Build modifier bitmask from a KeyboardEvent (HID standard bit order)
// Bit 0=LCtrl 1=LShift 2=LAlt 3=LMeta  4=RCtrl 5=RShift 6=RAlt 7=RMeta
function modFromEvent(e) {
  var m = 0;
  if (e.ctrlKey)  m |= 0x01;
  if (e.shiftKey) m |= 0x02;
  if (e.altKey)   m |= 0x04;
  if (e.metaKey)  m |= 0x08;
  return m;
}

// Convert a printable character to its HID Usage ID (Usage Page 0x07).
// Letters: 'a'-'z' → 0x04-0x1D  (ASCII_lower - 93)
// Digits : '1'-'9' → 0x1E-0x26, '0' → 0x27
// Symbols: mapped explicitly to match a US-QWERTY layout.
// Returns 0 if unknown (caller should skip sending).
function charToHid(c) {
  var lc = c.toLowerCase();
  var code = lc.charCodeAt(0);
  // a-z
  if (code >= 97 && code <= 122) return code - 93; // 'a'=4 … 'z'=29
  // 1-9
  if (code >= 49 && code <= 57)  return code - 19; // '1'=0x1E … '9'=0x26
  // 0
  if (c === '0') return 0x27;
  // Symbols (unshifted key face, same HID code for shifted variant)
  var sym = {
    '-':0x2D, '=':0x2E,
    '[':0x2F, ']':0x30, '\\':0x31,
    ';':0x33, "'":0x34,
    '`':0x35,
    ',':0x36, '.':0x37, '/':0x38,
    // Shifted symbols share the same HID usage as their unshifted counterpart
    '_':0x2D, '+':0x2E,
    '{':0x2F, '}':0x30, '|':0x31,
    ':':0x33, '"':0x34,
    '~':0x35,
    '<':0x36, '>':0x37, '?':0x38,
    '!':0x1E, '@':0x1F, '#':0x20, '$':0x21, '%':0x22,
    '^':0x23, '&':0x24, '*':0x25, '(':0x26, ')':0x27,
  };
  return sym[c] || 0;
}

// Send a key with modifiers, bypassing activeModifiers panel
function sendKeyEvent(e) {
  var key = e.key;
  var mod = modFromEvent(e);

  // --- Modifier-only presses: nothing to send yet ---
  if (key === 'Control' || key === 'Shift' || key === 'Alt' ||
      key === 'Meta' || key === 'OS') return;

  // --- Named special key with optional modifiers ---
  var hid = KEY_HID[key];
  if (hid) {
    wsSend('S:' + hid + (mod ? ',' + mod : ''));
    return;
  }

  // --- Modifier+printable char combo (Ctrl+A, Ctrl+`, Alt+F, etc.) ---
  // Must convert char → HID usage code (NOT ASCII) because H: on the ESP32
  // calls hidKeyboardCombo(mod, usage) which passes usage directly to HID.
  if (key.length === 1) {
    if (mod & ~0x02) {
      // Has Ctrl, Alt or Meta (pure Shift just changes the char — skip it)
      var hidCode = charToHid(key);
      if (hidCode) wsSend('H:' + mod + ',' + hidCode);
    }
    // Pure character (no Ctrl/Alt/Meta) — handled by the 'input' event
    return;
  }
}

// ── document-level keydown capture (fires before anything else on the page) ──
// Grabbing at document level with capture=true means we see the event first
// and can call stopImmediatePropagation so nothing else (form submit, tab
// focus change, browser shortcut) can act on it while liveInput is focused.
document.addEventListener('keydown', function(e) {
  if (!kbActive) return;      // only intercept while keyboard input is active

  var key = e.key;

  // Always block default browser actions for every key while we are active
  e.preventDefault();
  e.stopImmediatePropagation();

  if (composing) return;      // let IME finish; we'll grab text via input event

  // Ctrl+` (backtick) — key='`', ctrlKey=true, often blocked on Android
  // We still try; if Android killed it this handler won't fire, but on
  // desktop/PC-keyboard-connected Android devices it will work.
  sendKeyEvent(e);

}, true);  // <── capture phase

// ── input event: the ONLY reliable event for printable chars on Android ──
// Android GBoard fires key='Unidentified' in keydown then fires 'input' with
// the actual character.  We consume it here and clear the box each time.
liveInput.addEventListener('input', function(e) {
  if (composing) return; // IME not done yet

  // e.data holds the new character(s) added
  // e.inputType tells us what happened
  var t = e.inputType || '';

  if (t === 'deleteContentBackward' || t === 'deleteWordBackward') {
    wsSend('S:' + KEY_HID['Backspace']);
  } else if (t === 'deleteContentForward') {
    wsSend('S:' + KEY_HID['Delete']);
  } else if (t === 'insertLineBreak' || t === 'insertParagraph') {
    // Enter key via inputType (Android path)
    wsSend('S:' + KEY_HID['Enter']);
  } else if (e.data) {
    // Normal character(s) — could be multi-char if autocorrect inserts a word
    for (var i = 0; i < e.data.length; i++) {
      wsSend('K:' + e.data[i]);
    }
  }

  // Always wipe the box so the next event starts fresh
  // Use setTimeout 0 so the browser finishes its own value update first
  setTimeout(function() { liveInput.value = ''; }, 0);
});

// ── IME composition (CJK, autocorrect word replacement) ──
// During composition the interim text must NOT be sent keystroke-by-keystroke.
// We wait for compositionend which delivers the final committed string.
liveInput.addEventListener('compositionstart', function() { composing = true; });
liveInput.addEventListener('compositionupdate', function() { composing = true; });
liveInput.addEventListener('compositionend', function(e) {
  composing = false;
  var text = e.data || '';
  for (var i = 0; i < text.length; i++) {
    wsSend('K:' + text[i]);
  }
  setTimeout(function() { liveInput.value = ''; }, 0);
});

// ── Focus / blur tracking ──
// liveInput starts readonly so accidental taps never pop the keyboard.
// Only focusVirtualKeyboard() or a deliberate tap on the input itself
// removes readonly and activates keyboard capture.

function activateKeyboard() {
  liveInput.removeAttribute('readonly');
  liveInput.removeAttribute('tabindex');
  liveInput.value = '';
  liveInput.focus();
  // iOS sometimes needs an explicit click to trigger the IME
  liveInput.click();
}

function deactivateKeyboard() {
  liveInput.setAttribute('readonly', '');
  liveInput.setAttribute('tabindex', '-1');
}

// Explicit tap on the input field itself → activate keyboard
liveInput.addEventListener('touchend', function(e) {
  e.stopPropagation();
  activateKeyboard();
}, {passive: true});

// Click (desktop/pointer devices)
liveInput.addEventListener('mousedown', function(e) {
  e.stopPropagation();
  activateKeyboard();
});

liveInput.addEventListener('focus', function() {
  // Guard: if still readonly (programmatic focus from somewhere else), bail out
  if (liveInput.hasAttribute('readonly')) { liveInput.blur(); return; }
  kbActive = true;
  liveInput.value = '';
  liveInput.style.borderColor = '#e94560';
  liveInput.style.boxShadow = '0 0 0 2px rgba(233,69,96,0.4)';
});

liveInput.addEventListener('blur', function() {
  kbActive = false;
  liveInput.style.borderColor = '';
  liveInput.style.boxShadow = '';
  // Re-arm readonly so next accidental focus won't reopen keyboard
  deactivateKeyboard();
});

// Keep the box empty so the cursor never lands on existing text
// (prevents Backspace from deleting previously-typed text that wasn't cleared)
liveInput.addEventListener('keyup', function() {
  setTimeout(function() { liveInput.value = ''; }, 0);
});

function focusVirtualKeyboard() {
  // Explicit user request via button — activate keyboard capture
  activateKeyboard();
}

function sendFullText() {
  var txt = document.getElementById('textPayload').value;
  if (txt.length > 0) {
    wsSend('T:' + txt);
    document.getElementById('textPayload').value = '';
  }
}

function sendSpecial(hidCode) {
  wsSend('S:' + hidCode + (activeModifiers ? (',' + activeModifiers) : ''));
}

function toggleMod(mask, btn) {
  if (activeModifiers & mask) {
    activeModifiers &= ~mask;
    btn.classList.remove('active');
  } else {
    activeModifiers |= mask;
    btn.classList.add('active');
  }
}

function sendCombo(mod, hidCode) {
  wsSend('H:' + mod + ',' + hidCode);
}
</script>
</body>
</html>
)rawliteral";

static const char SAVED_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="4;url=/">
<title>Saved</title>
<style>
body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;background:#1a1a2e;color:#eee;display:flex;align-items:center;justify-content:center;min-height:100vh;text-align:center;margin:0;padding:20px}
.box{background:#16213e;padding:40px 30px;border-radius:12px;box-shadow:0 4px 20px rgba(0,0,0,.4);max-width:400px;width:100%}
h2{color:#e94560;margin-bottom:12px}
p{color:#aaa;line-height:1.6}
</style>
</head>
<body>
<div class="box">
  <h2>✅ Settings Saved!</h2>
  <p>The ESP32-S3 is applying settings and restarting…</p>
  <p style="font-size:.85em;color:#e94560;margin-top:20px">Redirecting to dashboard in 4 seconds</p>
</div>
</body>
</html>
)rawliteral";
