/*
 * ═══════════════════════════════════════════════════════════════════════════
 * ESP32-S3 WiFi Dongle for Keyboard and Mouse
 * ═══════════════════════════════════════════════════════════════════════════
 * Turns an ESP32-S3 into a plug-and-play USB Composite Mouse & Keyboard dongle
 * with a high-performance web dashboard hosted over WiFi (AP or Station mode).
 *
 * Open http://esp-mouse.local or http://192.168.4.1 from any smartphone, tablet,
 * or computer to control your PC with a low-latency touch trackpad & keyboard.
 *
 * ── Features ───────────────────────────────────────────────────────────────
 *   • Plug & Play USB Composite HID (Mouse + Keyboard) — zero PC drivers.
 *   • Touch Trackpad with multi-touch gestures:
 *       - 1-finger move: cursor navigation with smooth acceleration
 *       - 1-finger tap: left click
 *       - 1-finger double-tap & hold: click-and-drag (select text / move windows)
 *       - 2-finger tap: right click (context menu)
 *       - 2-finger drag: vertical scroll wheel
 *   • Fullscreen Mode:
 *       - Stretches trackpad across full phone screen (100vw x 100vh)
 *       - Floating overlay controls (Exit, Pop-up Keyboard, Thumb Left/Right click)
 *   • Mobile Virtual Keyboard & Live Keystroke Forwarding:
 *       - Tap to pop up native phone keyboard
 *       - Full text & command paste box
 *       - Special keys (Esc, Tab, Backspace, Enter, Del, Space)
 *       - Arrow cluster (Up, Down, Left, Right, Home, End)
 *       - Sticky modifiers (Ctrl, Alt, Shift, Win) & Hotkey combos (Ctrl+C, etc.)
 *   • Settings & Network Panel (matching ESP32S3-FTP-SDCard):
 *       - AP Mode (hotspot) vs Station Mode (join local router)
 *       - mDNS responder (http://esp-mouse.local)
 *       - Persistent settings in NVS Preferences + Auto-fallback to AP if router fails
 *       - Save & Restart workflow with automatic redirect
 *   • Low-Latency Real-Time Protocol:
 *       - Sub-5ms WebSocket connection (RFC 6455) on port 81 (zero library dependencies)
 *
 * ── Board Settings (Arduino IDE) ───────────────────────────────────────────
 *   Board:           "ESP32S3 Dev Module"
 *   USB Mode:        "Hardware CDC and JTAG" or "USB-OTG (TinyUSB)"
 *   USB CDC On Boot: "Disabled" (or "Enabled" for debug logs)
 *   Flash Size:      "16MB" (or "4MB" / board default)
 *   PSRAM:           "OPI PSRAM" (if equipped)
 * ───────────────────────────────────────────────────────────────────────────
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>

#include "WebSocketServer.h"
#include "WebPage.h"

// ─────────────────────────── USB Stack Selection ───────────────────────────
// 1 = EspUsbDevice (recommended, using /home/yogesh/Arduino/libraries/EspUsbDevice)
// 0 = Built-in Arduino-ESP32 Core USB HID (USB.h, USBHIDMouse.h, USBHIDKeyboard.h)
#define USE_ESP_USB_DEVICE 1

#if USE_ESP_USB_DEVICE
  #include <EspUsbDevice.h>
  static EspUsbDevice            usbDevice;
  static EspUsbDeviceHidKeyboard usbKeyboard(usbDevice);
  static EspUsbDeviceHidMouse    usbMouse(usbDevice);
#else
  #include "USB.h"
  #include "USBHIDMouse.h"
  #include "USBHIDKeyboard.h"
  static USBHIDMouse    usbMouse;
  static USBHIDKeyboard usbKeyboard;
#endif

// ─────────────────────────── RGB LED Control (Always OFF) ──────────────────
#ifndef RGB_BUILTIN
#define RGB_BUILTIN 48
#endif

inline void turnOffLED() {
  neopixelWrite(RGB_BUILTIN, 0, 0, 0);
  neopixelWrite(48, 0, 0, 0);
  neopixelWrite(38, 0, 0, 0);
}

// ─────────────────────────── Circle Mouse Motion (20px Radius) ─────────────
bool          circleMotionActive = false;
unsigned long lastCircleTick     = 0;
int           circleStep         = 0;
const int     CIRCLE_STEPS       = 60;    // 60 steps per revolution
const float   CIRCLE_RADIUS      = 20.0f; // 20px radius
float         circleLastX        = CIRCLE_RADIUS;
float         circleLastY        = 0.0f;

// ─────────────────────────── Preferences & Network ─────────────────────────
Preferences prefs;
bool   useAPMode;
String apSSID, apPassword;
String staSSID, staPassword;
String mdnsHost;

WebServer       webServer(80);
WebSocketServer wsServer(81);

// ─────────────────────────── HID Wrapper Functions ─────────────────────────
void initUSB() {
#if USE_ESP_USB_DEVICE
  EspUsbDeviceConfig config;
  config.vid          = 0x303a;
  config.pid          = 0x4003;
  config.manufacturer = "Espressif";
  config.product      = "ESP32-S3 Air Input Dongle";
  config.serialNumber = "esp32s3-air-input";
  if (!usbDevice.begin(config)) {
    Serial.printf("[USB] USB HID begin failed: %s\n", usbDevice.lastErrorName());
  } else {
    Serial.printf("[USB] USB Composite Keyboard + Mouse ready (kb id=%u, mouse id=%u)\n",
                  usbKeyboard.compositeReportId(), usbMouse.compositeReportId());
  }
#else
  usbMouse.begin();
  usbKeyboard.begin();
  USB.begin();
  Serial.println("[USB] Core USBHID Keyboard + Mouse initialized.");
#endif
}

bool isUSBReady() {
#if USE_ESP_USB_DEVICE
  return usbDevice.ready();
#else
  return true;
#endif
}

inline void hidMouseMove(int8_t dx, int8_t dy, int8_t wheel) {
#if USE_ESP_USB_DEVICE
  if (usbDevice.ready()) {
    usbMouse.move(dx, dy, wheel);
  }
#else
  usbMouse.move(dx, dy, wheel);
#endif
}

inline void hidMouseMove(int8_t dx, int8_t dy) {
  hidMouseMove(dx, dy, 0);
}

void hidMouseClick(uint8_t btn) {
#if USE_ESP_USB_DEVICE
  if (usbDevice.ready()) {
    uint8_t mask = (btn == 2) ? ESP_USB_DEVICE_MOUSE_RIGHT :
                   (btn == 3) ? ESP_USB_DEVICE_MOUSE_MIDDLE :
                                ESP_USB_DEVICE_MOUSE_LEFT;
    usbMouse.click(mask);
  }
#else
  uint8_t mask = (btn == 2) ? MOUSE_RIGHT : (btn == 3) ? MOUSE_MIDDLE : MOUSE_LEFT;
  usbMouse.click(mask);
#endif
}

void hidMousePress(uint8_t btn) {
#if USE_ESP_USB_DEVICE
  if (usbDevice.ready()) {
    uint8_t mask = (btn == 2) ? ESP_USB_DEVICE_MOUSE_RIGHT :
                   (btn == 3) ? ESP_USB_DEVICE_MOUSE_MIDDLE :
                                ESP_USB_DEVICE_MOUSE_LEFT;
    usbMouse.press(mask);
  }
#else
  uint8_t mask = (btn == 2) ? MOUSE_RIGHT : (btn == 3) ? MOUSE_MIDDLE : MOUSE_LEFT;
  usbMouse.press(mask);
#endif
}

void hidMouseRelease(uint8_t btn) {
#if USE_ESP_USB_DEVICE
  if (usbDevice.ready()) {
    uint8_t mask = (btn == 2) ? ESP_USB_DEVICE_MOUSE_RIGHT :
                   (btn == 3) ? ESP_USB_DEVICE_MOUSE_MIDDLE :
                                ESP_USB_DEVICE_MOUSE_LEFT;
    usbMouse.release(mask);
  }
#else
  uint8_t mask = (btn == 2) ? MOUSE_RIGHT : (btn == 3) ? MOUSE_MIDDLE : MOUSE_LEFT;
  usbMouse.release(mask);
#endif
}

void hidMouseWheel(int8_t delta) {
#if USE_ESP_USB_DEVICE
  if (usbDevice.ready()) {
    usbMouse.wheel(delta);
  }
#else
  usbMouse.move(0, 0, delta);
#endif
}

void hidMouseReleaseAll() {
#if USE_ESP_USB_DEVICE
  if (usbDevice.ready()) usbMouse.releaseAll();
#else
  usbMouse.release(MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE);
#endif
}

void hidKeyboardTapChar(char c) {
#if USE_ESP_USB_DEVICE
  if (usbDevice.ready()) usbKeyboard.tapKey(c);
#else
  usbKeyboard.write(c);
#endif
}

void hidKeyboardWriteString(const String &str) {
#if USE_ESP_USB_DEVICE
  if (usbDevice.ready()) usbKeyboard.write(str.c_str());
#else
  usbKeyboard.print(str);
#endif
}

inline void hidKeyboardTapSpecial(uint8_t usage, uint8_t modifiers) {
#if USE_ESP_USB_DEVICE
  if (usbDevice.ready()) {
    usbKeyboard.tapUsage(usage, modifiers);
  }
#else
  usbKeyboard.press(usage);
  delay(10);
  usbKeyboard.release(usage);
#endif
}

inline void hidKeyboardTapSpecial(uint8_t usage) {
  hidKeyboardTapSpecial(usage, 0);
}

void hidKeyboardCombo(uint8_t modifiers, uint8_t usage) {
#if USE_ESP_USB_DEVICE
  if (usbDevice.ready()) {
    usbKeyboard.pressUsage(usage, modifiers);
    delay(20);
    usbKeyboard.releaseUsage(usage);
    usbKeyboard.releaseAll();
  }
#else
  usbKeyboard.press(usage);
  delay(20);
  usbKeyboard.releaseAll();
#endif
}

void hidKeyboardReleaseAll() {
#if USE_ESP_USB_DEVICE
  if (usbDevice.ready()) usbKeyboard.releaseAll();
#else
  usbKeyboard.releaseAll();
#endif
}

// ─────────────────────────── NVS Configuration ─────────────────────────────
void loadPrefs() {
  prefs.begin("airinput", false);
  useAPMode   = prefs.getBool  ("ap_mode",  true);
  apSSID      = prefs.getString("ap_ssid",  "ESP32-AirInput");
  apPassword  = prefs.getString("ap_pass",  "12345678");
  staSSID     = prefs.getString("sta_ssid", "");
  staPassword = prefs.getString("sta_pass", "");
  mdnsHost    = prefs.getString("mdns",     "esp-mouse");
}

// ─────────────────────────── Web Page Builder ──────────────────────────────
String buildPage() {
  String html = FPSTR(MAIN_HTML);

  html.replace("%USB_STATUS%", isUSBReady() ? "Ready ✓" : "Connected");
  html.replace("%WIFI_MODE%",  useAPMode ? "Access Point (AP)" : "Station (STA)");
  html.replace("%WIFI_IP%",    useAPMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString());
  html.replace("%MDNS_HOST%",  mdnsHost);

  html.replace("%AP_CHK%",   useAPMode ? "checked" : "");
  html.replace("%STA_CHK%",  useAPMode ? "" : "checked");
  html.replace("%AP_SSID%",  apSSID);
  html.replace("%AP_PASS%",  apPassword);
  html.replace("%STA_SSID%", staSSID);
  html.replace("%STA_PASS%", staPassword);

  return html;
}

void handleRoot() {
  webServer.send(200, "text/html", buildPage());
}

void handleSave() {
  if (webServer.hasArg("mode"))     prefs.putBool  ("ap_mode",  webServer.arg("mode") == "ap");
  if (webServer.hasArg("ap_ssid")) prefs.putString("ap_ssid",  webServer.arg("ap_ssid"));
  if (webServer.hasArg("ap_pass")) prefs.putString("ap_pass",  webServer.arg("ap_pass"));
  if (webServer.hasArg("sta_ssid"))prefs.putString("sta_ssid", webServer.arg("sta_ssid"));
  if (webServer.hasArg("sta_pass"))prefs.putString("sta_pass", webServer.arg("sta_pass"));
  if (webServer.hasArg("mdns"))    prefs.putString("mdns",     webServer.arg("mdns"));

  Serial.println("[Web] Settings saved! Restarting ESP32-S3...");
  webServer.send(200, "text/html", FPSTR(SAVED_HTML));
  delay(600);
  ESP.restart();
}

void handleNotFound() {
  webServer.sendHeader("Location", "/", true);
  webServer.send(302, "text/plain", "");
}

// ─────────────────────────── WebSocket Message Handler ─────────────────────
void handleWebSocketMessage(uint8_t clientId, const String &msg) {
  if (msg.length() < 2) return;

  char cmd = msg.charAt(0);

  if (cmd == 'M' && msg.charAt(1) == ':') {
    // Relative Mouse Move: "M:dx,dy"
    int comma = msg.indexOf(',', 2);
    if (comma != -1) {
      int dx = msg.substring(2, comma).toInt();
      int dy = msg.substring(comma + 1).toInt();
      hidMouseMove((int8_t)constrain(dx, -127, 127), (int8_t)constrain(dy, -127, 127));
    }
  } else if (cmd == 'C' && msg.charAt(1) == ':') {
    // Mouse Click: "C:btn"
    uint8_t btn = (uint8_t)msg.substring(2).toInt();
    hidMouseClick(btn);
  } else if (cmd == 'D' && msg.charAt(1) == ':') {
    // Mouse Button Down: "D:btn"
    uint8_t btn = (uint8_t)msg.substring(2).toInt();
    hidMousePress(btn);
  } else if (cmd == 'U' && msg.charAt(1) == ':') {
    // Mouse Button Up: "U:btn"
    uint8_t btn = (uint8_t)msg.substring(2).toInt();
    hidMouseRelease(btn);
  } else if (cmd == 'W' && msg.charAt(1) == ':') {
    // Scroll Wheel: "W:delta"
    int delta = msg.substring(2).toInt();
    hidMouseWheel((int8_t)constrain(delta, -127, 127));
  } else if (cmd == 'K' && msg.charAt(1) == ':') {
    // Single Character: "K:c"
    if (msg.length() >= 3) {
      hidKeyboardTapChar(msg.charAt(2));
    }
  } else if (cmd == 'T' && msg.charAt(1) == ':') {
    // Full Text String: "T:hello world"
    String text = msg.substring(2);
    hidKeyboardWriteString(text);
  } else if (cmd == 'S' && msg.charAt(1) == ':') {
    // Special HID Usage: "S:code[,mod]"
    int comma = msg.indexOf(',', 2);
    uint8_t usage = (uint8_t)msg.substring(2, comma == -1 ? msg.length() : comma).toInt();
    uint8_t mod   = (comma != -1) ? (uint8_t)msg.substring(comma + 1).toInt() : 0;
    hidKeyboardTapSpecial(usage, mod);
  } else if (cmd == 'H' && msg.charAt(1) == ':') {
    // Hotkey Combo: "H:mod,usage"
    int comma = msg.indexOf(',', 2);
    if (comma != -1) {
      uint8_t mod   = (uint8_t)msg.substring(2, comma).toInt();
      uint8_t usage = (uint8_t)msg.substring(comma + 1).toInt();
      hidKeyboardCombo(mod, usage);
    }
  } else if (cmd == 'J' && msg.charAt(1) == ':') {
    // Circle Mouse Motion: "J:1" or "J:0"
    circleMotionActive = (msg.charAt(2) == '1');
    if (circleMotionActive) {
      circleStep  = 0;
      circleLastX = CIRCLE_RADIUS;
      circleLastY = 0.0f;
    }
    wsServer.broadcast("JIGGLER:" + String(circleMotionActive ? "1" : "0"));
  }
}

// ─────────────────────────── Setup ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Turn off onboard NeoPixel (Always OFF)
  turnOffLED();

  delay(600);
  Serial.println("\n╔════════════════════════════════════════════════════════╗");
  Serial.println("║   ESP32-S3 WiFi Dongle for Keyboard and Mouse          ║");
  Serial.println("╚════════════════════════════════════════════════════════╝\n");

  // 1. Initialize USB Composite HID
  initUSB();

  // 2. Load stored settings from Preferences
  loadPrefs();
  Serial.printf("[CFG] WiFi Mode: %s\n", useAPMode ? "Access Point (AP)" : "Station (STA)");
  Serial.printf("[CFG] Hostname : %s.local\n", mdnsHost.c_str());

  // 3. Connect WiFi
  if (useAPMode) {
    Serial.printf("[WiFi] Starting AP hotspot: %s\n", apSSID.c_str());
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID.c_str(), apPassword.c_str());
    delay(400);
    Serial.printf("[WiFi] AP Mode Active! IP: %s\n", WiFi.softAPIP().toString().c_str());
  } else {
    Serial.printf("[WiFi] Connecting to router SSID '%s'...", staSSID.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(staSSID.c_str(), staPassword.c_str());

    int timeout = 40; // 20 seconds timeout
    while (WiFi.status() != WL_CONNECTED && timeout-- > 0) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Connection failed! Falling back to Access Point mode...");
      useAPMode = true;
      WiFi.mode(WIFI_AP);
      WiFi.softAP(apSSID.c_str(), apPassword.c_str());
      delay(400);
      Serial.printf("[WiFi] Fallback AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    } else {
      Serial.printf("[WiFi] Connected! Station IP: %s\n", WiFi.localIP().toString().c_str());
    }
  }

  // 4. Start mDNS Responder
  if (MDNS.begin(mdnsHost.c_str())) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws",   "tcp", 81);
    Serial.printf("[mDNS] http://%s.local is ready!\n", mdnsHost.c_str());
  } else {
    Serial.println("[mDNS] Responder failed to start; use IP directly.");
  }

  // 5. Start Web Server
  webServer.on("/",     HTTP_GET,  handleRoot);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  Serial.println("[Web] HTTP dashboard running on port 80");

  // 6. Start WebSocket Server
  wsServer.onMessage(handleWebSocketMessage);
  wsServer.onConnect([](uint8_t clientId) {
    Serial.printf("[WS] Client #%u connected\n", clientId);
    wsServer.sendText(clientId, "JIGGLER:" + String(circleMotionActive ? "1" : "0"));
    wsServer.broadcast("CLIENTS:" + String(wsServer.activeClients()));
  });
  wsServer.onDisconnect([](uint8_t clientId) {
    Serial.printf("[WS] Client #%u disconnected\n", clientId);
    // Safety cleanup: release held buttons/keys
    hidMouseReleaseAll();
    hidKeyboardReleaseAll();
    wsServer.broadcast("CLIENTS:" + String(wsServer.activeClients()));
  });
  wsServer.begin();
  Serial.println("[WS] WebSocket engine running on port 81");

  String accessUrl = "http://" + (useAPMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString());
  Serial.printf("\nReady! Open in your phone/browser: %s or http://%s.local\n\n",
                accessUrl.c_str(), mdnsHost.c_str());
}

// ─────────────────────────── Loop ──────────────────────────────────────────
void loop() {
  webServer.handleClient();
  wsServer.loop();

  // ── Continuous Circle Mouse Motion (20px radius, zero drift) ────────────
  if (circleMotionActive && isUSBReady()) {
    unsigned long now = millis();
    if (now - lastCircleTick >= 25) { // 25ms interval = ~1.5 seconds per 360° circle
      lastCircleTick = now;
      circleStep = (circleStep + 1) % CIRCLE_STEPS;
      float angle   = 2.0f * PI * (float)circleStep / (float)CIRCLE_STEPS;
      float targetX = CIRCLE_RADIUS * cos(angle);
      float targetY = CIRCLE_RADIUS * sin(angle);
      int dx = (int)round(targetX - circleLastX);
      int dy = (int)round(targetY - circleLastY);
      if (dx != 0 || dy != 0) {
        hidMouseMove((int8_t)dx, (int8_t)dy);
        circleLastX += (float)dx;
        circleLastY += (float)dy;
      }
    }
  }
}
