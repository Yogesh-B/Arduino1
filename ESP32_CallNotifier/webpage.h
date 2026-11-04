#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <Arduino.h>

// ── Embedded Web Application (HTML / CSS / JavaScript) ───────────────────────
// Kept in this separate header file so that the Arduino IDE preprocessor does NOT
// scan JavaScript 'function' keywords as C++ prototypes.
static const char HTML_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32-S3 Call Notifier</title>
<style>
:root{--bg:#121214;--card:#1c1c20;--border:#2e2e34;--text:#e4e4e7;--muted:#a1a1aa;--primary:#3b82f6;--primary-hover:#2563eb;--danger:#ef4444;--success:#22c55e;--warn:#f59e0b;}
*{box-sizing:border-box;margin:0;padding:0;}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;background:var(--bg);color:var(--text);padding:16px;display:flex;justify-content:center;}
.app{width:100%;max-width:680px;display:flex;flex-direction:column;gap:16px;}
header{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;padding:12px 18px;background:var(--card);border:1px solid var(--border);border-radius:12px;gap:10px;}
.title{font-size:18px;font-weight:700;display:flex;align-items:center;gap:8px;color:#fff;}
.badges{display:flex;align-items:center;gap:8px;font-size:12px;font-family:monospace;}
.badge{padding:4px 9px;border-radius:20px;background:#27272a;display:inline-flex;align-items:center;gap:6px;}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block;}
.dot.ok{background:var(--success);box-shadow:0 0 8px var(--success);}
.dot.warn{background:var(--warn);box-shadow:0 0 8px var(--warn);}
.dot.fail{background:var(--danger);}
.dot.ring{background:var(--danger);animation:pulse 0.8s infinite;}
@keyframes pulse{0%{opacity:1;transform:scale(1);}50%{opacity:0.4;transform:scale(1.3);}100%{opacity:1;transform:scale(1);}}
.card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:20px;}
.card-head{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px;border-bottom:1px solid var(--border);padding-bottom:10px;}
.card-head h2{font-size:16px;font-weight:600;display:flex;align-items:center;gap:8px;}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:12px;}
@media(max-width:540px){.grid2{grid-template-columns:1fr;}}
.stat-box{background:#151518;border:1px solid var(--border);padding:10px 14px;border-radius:8px;font-size:13px;line-height:1.6;}
.stat-box b{color:var(--muted);font-weight:500;font-size:11px;text-transform:uppercase;display:block;}
.stat-box span{color:#fff;font-family:monospace;font-size:13px;}
label{display:block;margin-top:10px;font-size:12px;color:var(--muted);text-transform:uppercase;}
input,select{width:100%;padding:10px 12px;margin-top:4px;border:1px solid var(--border);border-radius:8px;background:#151518;color:#fff;font-size:14px;}
input:focus{outline:none;border-color:var(--primary);}
button{padding:10px 16px;border:none;border-radius:8px;background:var(--primary);color:#fff;font-weight:600;font-size:13px;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;gap:6px;transition:0.15s;}
button:hover{background:var(--primary-hover);}
button.sec{background:#27272a;color:#e4e4e7;}
button.sec:hover{background:#3f3f46;}
button.danger{background:var(--danger);}
button.danger:hover{background:#dc2626;}
button:disabled{opacity:0.5;cursor:not-allowed;}
.btn-row{display:flex;gap:10px;margin-top:14px;}
.call-modal{position:fixed;top:20px;left:50%;transform:translateX(-50%);width:92%;max-width:500px;background:#1f1618;border:2px solid var(--danger);border-radius:16px;padding:22px;box-shadow:0 10px 30px rgba(239,68,68,0.3);z-index:9999;display:none;animation:slideDown 0.3s ease;}
@keyframes slideDown{from{transform:translate(-50%,-30px);opacity:0;}to{transform:translate(-50%,0);opacity:1;}}
.call-modal.active{display:block;}
.call-modal-inner{display:flex;flex-direction:column;align-items:center;text-align:center;}
.call-icon{font-size:42px;margin-bottom:8px;display:inline-block;animation:wobble 0.8s infinite;}
@keyframes wobble{0%,100%{transform:rotate(0deg);}20%{transform:rotate(-15deg);}40%{transform:rotate(15deg);}60%{transform:rotate(-10deg);}80%{transform:rotate(10deg);}}
.call-title{font-size:14px;text-transform:uppercase;letter-spacing:1.5px;color:var(--danger);font-weight:700;}
.caller-id{font-size:26px;font-weight:800;color:#fff;margin:6px 0;letter-spacing:0.5px;}
.caller-contact{font-size:15px;color:var(--muted);margin-bottom:14px;}
.history-table{width:100%;border-collapse:collapse;margin-top:8px;font-size:13px;}
.history-table th,.history-table td{padding:8px 10px;text-align:left;border-bottom:1px solid var(--border);}
.history-table th{color:var(--muted);font-size:11px;text-transform:uppercase;}
.hint{font-size:12px;color:var(--muted);margin-top:6px;line-height:1.5;}
.row-between{display:flex;justify-content:space-between;align-items:center;}
.guide-box{background:#151518;border:1px solid var(--border);border-radius:8px;padding:12px 16px;margin-top:12px;font-size:13px;line-height:1.7;}
.guide-box ol{padding-left:18px;margin-top:6px;}
.guide-box li{margin-bottom:4px;}
.guide-box a{color:var(--primary);text-decoration:none;}
.guide-box a:hover{text-decoration:underline;}
</style>
</head>
<body>

<!-- Incoming Call Popup Banner -->
<div id="call-modal" class="call-modal">
  <div class="call-modal-inner">
    <div class="call-icon">&#x1F4DE;</div>
    <div id="call-status-lbl" class="call-title">INCOMING PHONE CALL</div>
    <div id="call-number" class="caller-id">Unknown Caller</div>
    <div id="call-contact" class="caller-contact">--</div>
    <div class="btn-row" style="margin-top:4px;">
      <button id="btn-silence" class="sec" onclick="silenceAudio()">&#x1F507; Silence Ring</button>
      <button class="danger" onclick="dismissCallModal()">Dismiss</button>
    </div>
  </div>
</div>

<div class="app">
  <header>
    <div class="title">&#x1F4E1; ESP32-S3 Call Notifier</div>
    <div class="badges">
      <div class="badge" title="SSE Stream Status">
        <span id="sse-dot" class="dot warn"></span>
        <span id="sse-txt">SSE: Connecting</span>
      </div>
      <div class="badge" title="BLE Status">
        <span id="ble-dot" class="dot fail"></span>
        <span id="ble-txt">BLE: Advertising</span>
      </div>
    </div>
  </header>

  <!-- Desktop Notifications & Audio Card -->
  <div class="card">
    <div class="card-head">
      <h2>&#x1F514; Desktop Notifications &amp; Audio</h2>
      <span id="notif-perm-badge" class="badge">Checking...</span>
    </div>
    <p class="hint">Incoming calls trigger high-priority alerts with caller ID. Web Audio plays instant telephone ringtones without any external audio files.</p>
    <div class="btn-row">
      <button id="btn-perm" onclick="requestNotificationPerm()">&#x1F514; Enable Desktop Notifications</button>
      <button class="sec" onclick="testNotification()">&#x25B6; Test Alert &amp; Sound</button>
    </div>
    <div id="chrome-flag-tip" class="hint" style="display:none;margin-top:10px;padding:8px 12px;background:#27272a;border-radius:6px;color:#fbbf24;">
      <b>Chrome/Edge Tip:</b> If notifications are blocked over HTTP, go to <code>chrome://flags/#unsafely-treat-insecure-origin-as-secure</code> and add your ESP32's IP (<span id="tip-ip">http://192.168.4.1</span>).
    </div>
  </div>

  <!-- Bluetooth Low Energy (Gadgetbridge) Card -->
  <div class="card">
    <div class="card-head">
      <h2>&#x1F4F1; Bluetooth Low Energy (Android Phone)</h2>
      <span id="ble-badge" class="badge">Advertising</span>
    </div>
    <div class="grid2">
      <div class="stat-box">
        <b>BLE Link Status</b>
        <span id="ble-state-display">Advertising (Ready to Pair)</span>
      </div>
      <div class="stat-box">
        <b>Advertised Device Name</b>
        <span>Bangle.js CallNotifier</span>
      </div>
    </div>

    <!-- Quick Gadgetbridge Setup Guide -->
    <div class="guide-box">
      <b>&#x1F4F2; How to Connect Your Android Phone:</b>
      <ol>
        <li>Install <a href="https://f-droid.org/packages/nodomain.freeyourgadget.gadgetbridge/" target="_blank">Gadgetbridge (F-Droid)</a> or from <a href="https://github.com/Freeyourgadget/Gadgetbridge/releases" target="_blank">GitHub</a> on your Android phone.</li>
        <li>Open Gadgetbridge and grant Notification &amp; Call permissions when asked.</li>
        <li>Tap the <b>'+'</b> button to scan for nearby devices.</li>
        <li>Select <b>"Bangle.js CallNotifier"</b> and confirm pairing.</li>
        <li>Done! Your phone will automatically forward all calls to this dashboard.</li>
      </ol>
    </div>
  </div>

  <!-- WiFi Configuration Card -->
  <div class="card">
    <div class="card-head">
      <h2>&#x1F4F6; WiFi Settings (AP + STA)</h2>
      <span id="wifi-status-badge" class="badge">AP Only</span>
    </div>
    <div class="grid2" style="margin-bottom:12px;">
      <div class="stat-box">
        <b>SoftAP Subnet</b>
        <span>192.168.4.1</span>
      </div>
      <div class="stat-box">
        <b>Station (LAN) IP</b>
        <span id="sta-ip-display">Not connected</span>
      </div>
    </div>
    <form id="wifi-form" onsubmit="saveWifi(event)">
      <label>Target WiFi SSID</label>
      <input type="text" id="wifi-ssid" placeholder="Enter home or office WiFi name" required>
      <label>Target WiFi Password</label>
      <input type="password" id="wifi-pwd" placeholder="Enter password">
      <button type="submit" style="margin-top:14px;width:100%;">&#x1F4BE; Save &amp; Connect (No Reboot)</button>
    </form>
    <div class="hint" style="margin-top:8px;">The ESP32 connects live to your WiFi without interrupting SoftAP clients.</div>
  </div>

  <!-- Quick Contacts / Phonebook Card -->
  <div class="card">
    <div class="card-head">
      <h2>&#x1F4D3; Quick Contacts (Phonebook)</h2>
    </div>
    <p class="hint">Assign friendly contact names to phone numbers. Saved securely in your browser.</p>
    <div class="grid2" style="margin-top:10px;">
      <input type="text" id="contact-num" placeholder="Number (e.g. +1234567890)">
      <input type="text" id="contact-name" placeholder="Name (e.g. Mom)">
    </div>
    <button style="margin-top:10px;width:100%;" onclick="addContact()">+ Add Contact</button>
    <div id="contacts-list" style="margin-top:12px;font-size:13px;"></div>
  </div>

  <!-- Recent Calls History Card -->
  <div class="card">
    <div class="card-head">
      <h2>&#x1F4CB; Recent Calls &amp; Alerts</h2>
      <button class="sec" style="font-size:11px;padding:4px 10px;" onclick="clearHistory()">Clear</button>
    </div>
    <table class="history-table">
      <thead>
        <tr><th>Time</th><th>Caller / Event</th><th>Status</th></tr>
      </thead>
      <tbody id="history-body">
        <tr><td colspan="3" style="text-align:center;color:var(--muted);padding:14px;">No call history yet</td></tr>
      </tbody>
    </table>
  </div>
</div>

<script>
// ── Web Audio Synthesizer (Realistic Telephone Bell) ─────────────────────────
let audioCtx = null;
let ringInterval = null;

function getAudioContext() {
  if (!audioCtx) {
    const AudioContextClass = window.AudioContext || window.webkitAudioContext;
    if (AudioContextClass) audioCtx = new AudioContextClass();
  }
  if (audioCtx && audioCtx.state === 'suspended') {
    audioCtx.resume();
  }
  return audioCtx;
}

function playTonePair(freq1, freq2, durationSec) {
  const ctx = getAudioContext();
  if (!ctx) return;
  const now = ctx.currentTime;
  const osc1 = ctx.createOscillator();
  const osc2 = ctx.createOscillator();
  const gain = ctx.createGain();

  osc1.type = 'sine';
  osc2.type = 'sine';
  osc1.frequency.setValueAtTime(freq1, now);
  osc2.frequency.setValueAtTime(freq2, now);

  gain.gain.setValueAtTime(0, now);
  gain.gain.linearRampToValueAtTime(0.25, now + 0.05);
  gain.gain.setValueAtTime(0.25, now + durationSec - 0.05);
  gain.gain.linearRampToValueAtTime(0.001, now + durationSec);

  osc1.connect(gain);
  osc2.connect(gain);
  gain.connect(ctx.destination);

  osc1.start(now);
  osc2.start(now);
  osc1.stop(now + durationSec);
  osc2.stop(now + durationSec);
}

function startRingtone() {
  stopRingtone();
  // Standard dual-tone ring cadence: 440Hz + 480Hz (two 0.8s chirps every 4s)
  const ringBurst = () => {
    playTonePair(440, 480, 0.8);
    setTimeout(() => { playTonePair(440, 480, 0.8); }, 1200);
  };
  ringBurst();
  ringInterval = setInterval(ringBurst, 4000);
}

function stopRingtone() {
  if (ringInterval) {
    clearInterval(ringInterval);
    ringInterval = null;
  }
}

function silenceAudio() {
  stopRingtone();
  document.getElementById('btn-silence').innerText = 'Silenced ✓';
}

// ── Quick Contacts (LocalStorage) ────────────────────────────────────────────
function getContacts() {
  try { return JSON.parse(localStorage.getItem('esp32_contacts') || '{}'); } catch(e){ return {}; }
}
function saveContacts(c) {
  localStorage.setItem('esp32_contacts', JSON.stringify(c));
  renderContacts();
}
function addContact() {
  const num = document.getElementById('contact-num').value.trim();
  const name = document.getElementById('contact-name').value.trim();
  if (!num || !name) return;
  const c = getContacts();
  c[num.replace(/\s+/g,'')] = name;
  saveContacts(c);
  document.getElementById('contact-num').value = '';
  document.getElementById('contact-name').value = '';
}
function deleteContact(num) {
  const c = getContacts();
  delete c[num];
  saveContacts(c);
}
function resolveCaller(rawNum, rawName) {
  if (!rawNum && !rawName) return { name: 'Unknown Caller', number: 'Private / Withheld' };
  const clean = (rawNum || '').replace(/\s+/g,'');
  const c = getContacts();
  for (let key in c) {
    if (clean && (clean.endsWith(key) || key.endsWith(clean))) return { name: c[key], number: rawNum };
  }
  if (rawName && rawName.length > 0) {
    return { name: rawName, number: rawNum || rawName };
  }
  return { name: rawNum || 'Unknown Caller', number: rawNum || 'Private' };
}
function renderContacts() {
  const c = getContacts();
  const list = document.getElementById('contacts-list');
  const keys = Object.keys(c);
  if (keys.length === 0) {
    list.innerHTML = '<span style="color:var(--muted)">No custom contacts added.</span>';
    return;
  }
  let h = '';
  for (let k of keys) {
    h += `<div class="row-between" style="padding:6px 0;border-bottom:1px solid #27272a;">
      <span><b>${escapeHtml(c[k])}</b> <span style="color:var(--muted);font-family:monospace;margin-left:6px;">${escapeHtml(k)}</span></span>
      <span style="color:var(--danger);cursor:pointer;font-size:12px;" onclick="deleteContact('${escapeHtml(k)}')">&#x2715;</span>
    </div>`;
  }
  list.innerHTML = h;
}

// ── Call History (LocalStorage) ──────────────────────────────────────────────
function getHistory() {
  try { return JSON.parse(localStorage.getItem('esp32_history') || '[]'); } catch(e){ return []; }
}
function addHistory(num, name, status) {
  const h = getHistory();
  const now = new Date();
  const timeStr = now.toLocaleTimeString([], { hour:'2-digit', minute:'2-digit', second:'2-digit' });
  const info = resolveCaller(num, name);
  const display = info.name !== info.number ? `${info.name} (${info.number})` : info.number;
  h.unshift({ time: timeStr, caller: display, status: status });
  if (h.length > 25) h.pop();
  localStorage.setItem('esp32_history', JSON.stringify(h));
  renderHistory();
}
function clearHistory() {
  localStorage.removeItem('esp32_history');
  renderHistory();
}
function renderHistory() {
  const h = getHistory();
  const body = document.getElementById('history-body');
  if (h.length === 0) {
    body.innerHTML = '<tr><td colspan="3" style="text-align:center;color:var(--muted);padding:14px;">No call history yet</td></tr>';
    return;
  }
  let html = '';
  for (let item of h) {
    const isMissed = item.status.toLowerCase().includes('missed') || item.status.toLowerCase().includes('ended');
    html += `<tr>
      <td style="font-family:monospace;color:var(--muted);">${escapeHtml(item.time)}</td>
      <td style="font-weight:600;">${escapeHtml(item.caller)}</td>
      <td style="color:${isMissed ? 'var(--danger)' : 'var(--success)'};">${escapeHtml(item.status)}</td>
    </tr>`;
  }
  body.innerHTML = html;
}

// ── Desktop Notifications API ────────────────────────────────────────────────
function updateNotifPermBadge() {
  const badge = document.getElementById('notif-perm-badge');
  const tip = document.getElementById('chrome-flag-tip');
  if (!('Notification' in window)) {
    badge.innerText = 'Unsupported';
    badge.className = 'badge fail';
    return;
  }
  if (Notification.permission === 'granted') {
    badge.innerText = 'Active ✓';
    badge.className = 'badge ok';
    tip.style.display = 'none';
  } else if (Notification.permission === 'denied') {
    badge.innerText = 'Blocked ✕';
    badge.className = 'badge fail';
    tip.style.display = 'block';
  } else {
    badge.innerText = 'Action Needed';
    badge.className = 'badge warn';
  }
}

function requestNotificationPerm() {
  getAudioContext();
  if (!('Notification' in window)) {
    alert('This browser does not support desktop notifications.');
    return;
  }
  Notification.requestPermission().then(p => {
    updateNotifPermBadge();
    if (p === 'granted') {
      new Notification('ESP32-S3 Call Notifier', {
        body: 'Desktop notifications are active!',
        icon: 'data:image/svg+xml,<svg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220%200%2032%2032%22><text y=%2226%22 font-size=%2226%22>📞</text></svg>'
      });
    }
  });
}

function showDesktopNotification(title, body) {
  if ('Notification' in window && Notification.permission === 'granted') {
    try {
      new Notification(title, {
        body: body,
        requireInteraction: true,
        icon: 'data:image/svg+xml,<svg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220%200%2032%2032%22><text y=%2226%22 font-size=%2226%22>📞</text></svg>'
      });
    } catch(e) { console.error('Notification error:', e); }
  }
}

function testNotification() {
  getAudioContext();
  startRingtone();
  setTimeout(stopRingtone, 2500);
  showDesktopNotification('📞 Test Call Alert', 'Desktop notifications and audio synthesizer are working!');
  handleIncomingCall('+1 (555) 019-2834', 'Jane Doe');
}

// ── Incoming Call Handlers ───────────────────────────────────────────────────
let currentCallNumber = '';
let currentCallName = '';

function handleIncomingCall(num, name) {
  currentCallNumber = num || currentCallNumber;
  currentCallName = name || currentCallName;
  const info = resolveCaller(currentCallNumber, currentCallName);
  document.getElementById('call-number').innerText = info.number;
  document.getElementById('call-contact').innerText = info.name !== info.number ? info.name : '';
  document.getElementById('call-status-lbl').innerText = 'INCOMING PHONE CALL';
  document.getElementById('call-status-lbl').style.color = 'var(--danger)';
  document.getElementById('call-modal').classList.add('active');
  document.getElementById('btn-silence').innerText = '🔇 Silence Ring';
  startRingtone();
  showDesktopNotification('📞 Incoming Call', `${info.name !== info.number ? info.name + ' (' + info.number + ')' : info.number} is calling...`);
}

function handleCallAnswered() {
  stopRingtone();
  document.getElementById('call-status-lbl').innerText = 'CALL IN PROGRESS';
  document.getElementById('call-status-lbl').style.color = 'var(--success)';
  addHistory(currentCallNumber, currentCallName, 'Answered');
}

function handleCallEnded() {
  stopRingtone();
  const modal = document.getElementById('call-modal');
  if (modal.classList.contains('active')) {
    addHistory(currentCallNumber, currentCallName, 'Missed / Ended');
  }
  document.getElementById('call-status-lbl').innerText = 'CALL ENDED';
  setTimeout(() => { dismissCallModal(); }, 2000);
}

function dismissCallModal() {
  stopRingtone();
  document.getElementById('call-modal').classList.remove('active');
  document.getElementById('call-status-lbl').style.color = 'var(--danger)';
  currentCallNumber = '';
  currentCallName = '';
}

// ── Server-Sent Events (SSE) Listener ────────────────────────────────────────
let eventSource = null;

function initSSE() {
  if (eventSource) eventSource.close();
  eventSource = new EventSource('/events');

  eventSource.addEventListener('open', () => {
    document.getElementById('sse-dot').className = 'dot ok';
    document.getElementById('sse-txt').innerText = 'SSE: Live';
  });

  eventSource.addEventListener('error', () => {
    document.getElementById('sse-dot').className = 'dot fail';
    document.getElementById('sse-txt').innerText = 'SSE: Reconnecting';
  });

  eventSource.addEventListener('incoming_call', (e) => {
    try {
      const data = JSON.parse(e.data || '{}');
      handleIncomingCall(data.number, data.name);
    } catch(err) { console.error(err); }
  });

  eventSource.addEventListener('call_answered', () => {
    handleCallAnswered();
  });

  eventSource.addEventListener('call_ended', () => {
    handleCallEnded();
  });

  eventSource.addEventListener('phone_notification', (e) => {
    try {
      const data = JSON.parse(e.data || '{}');
      showDesktopNotification(`💬 ${data.src || 'App Notification'}: ${data.title || ''}`, data.body || '');
      addHistory(data.src, data.title, 'Notification');
    } catch(err) { console.error(err); }
  });

  eventSource.addEventListener('ble_status', (e) => {
    try {
      const data = JSON.parse(e.data || '{}');
      updateBleUI(data);
    } catch(err) { console.error(err); }
  });
}

function updateBleUI(data) {
  const isConn = (data.state === 'connected');
  document.getElementById('ble-dot').className = isConn ? 'dot ok' : 'dot warn';
  document.getElementById('ble-txt').innerText = 'BLE: ' + (isConn ? 'Connected' : 'Advertising');
  document.getElementById('ble-badge').className = isConn ? 'badge ok' : 'badge';
  document.getElementById('ble-badge').innerText = isConn ? 'Phone Connected' : 'Advertising';
  document.getElementById('ble-state-display').innerText = isConn ? 'Connected to Android Phone ✓' : 'Advertising (Waiting for Gadgetbridge)';
}

function saveWifi(e) {
  e.preventDefault();
  const ssid = document.getElementById('wifi-ssid').value.trim();
  const pwd = document.getElementById('wifi-pwd').value;
  const badge = document.getElementById('wifi-status-badge');
  badge.innerText = 'Connecting...';
  badge.className = 'badge warn';
  fetch('/save-wifi', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: `ssid=${encodeURIComponent(ssid)}&pwd=${encodeURIComponent(pwd)}`
  })
  .then(r => r.json())
  .then(data => {
    alert('WiFi configuration saved. ESP32 is connecting to ' + ssid);
  });
}

// ── Periodic Status Polling ──────────────────────────────────────────────────
function pollStatus() {
  fetch('/status')
    .then(r => r.json())
    .then(d => {
      if (d.wifi) {
        const staSpan = document.getElementById('sta-ip-display');
        const badge = document.getElementById('wifi-status-badge');
        if (d.wifi.sta_connected && d.wifi.sta_ip && d.wifi.sta_ip !== '0.0.0.0') {
          staSpan.innerHTML = `<a href="http://${d.wifi.sta_ip}" target="_blank" style="color:var(--primary);text-decoration:none;">${d.wifi.sta_ip} ↗</a> (${d.wifi.sta_rssi} dBm)`;
          badge.innerText = 'Connected: ' + d.wifi.sta_ssid;
          badge.className = 'badge ok';
          document.getElementById('tip-ip').innerText = 'http://' + d.wifi.sta_ip;
        } else {
          staSpan.innerText = d.wifi.sta_ssid ? 'Connecting to ' + d.wifi.sta_ssid + '...' : 'Not configured';
          badge.innerText = 'AP Only';
          badge.className = 'badge';
        }
      }
      if (d.ble) {
        updateBleUI(d.ble);
      }
    })
    .catch(() => {});
}

function escapeHtml(s) {
  if (!s) return '';
  return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&#039;');
}

// ── Init on Page Load ────────────────────────────────────────────────────────
window.addEventListener('DOMContentLoaded', () => {
  renderContacts();
  renderHistory();
  updateNotifPermBadge();
  initSSE();
  pollStatus();
  setInterval(pollStatus, 3000);
});
</script>
</body>
</html>
)rawliteral";

#endif // WEBPAGE_H
