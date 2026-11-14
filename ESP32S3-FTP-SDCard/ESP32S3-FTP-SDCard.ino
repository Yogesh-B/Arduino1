/*
 * ESP32-S3 FTP Server with MicroSD Card (SPI)
 * + Web Configuration Portal at 192.168.4.1
 *
 * Board:  ESP32-S3-N16R8 (16MB Flash, 8MB PSRAM)
 * SD Card: 64GB exFAT via SPI-connected MicroSD module
 *
 * MicroSD Module Wiring (SPI):
 *   Module Pin  ->  ESP32-S3 GPIO
 *   ─────────────────────────────
 *   3.3V        ->  3.3V
 *   GND         ->  GND
 *   CS           ->  GPIO 10
 *   MOSI         ->  GPIO 11
 *   CLK (SCK)    ->  GPIO 12
 *   MISO         ->  GPIO 13
 *
 * NOTE: GPIOs 26-37 are reserved for PSRAM/Flash on N16R8 — do NOT use them.
 *
 * Dependencies: None beyond ESP32 Arduino Core 3.x (WebServer, Preferences,
 *               WiFi, SD, SPI all included in the core).
 *
 * First boot / AP mode:
 *   1. Connect to WiFi SSID "ESP32-FTP" (password: 12345678)
 *   2. Open browser → http://192.168.4.1
 *   3. Configure WiFi/FTP credentials and save.
 *   4. Connect FTP client to ESP32 IP, port 21.
 */

#include <WiFi.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>

// ─────────────────────────── SPI / FTP Constants ───────────────────────────

#define SD_CS           10
#define SD_MOSI         11
#define SD_SCK          12
#define SD_MISO         13

#ifndef RGB_BUILTIN
#define RGB_BUILTIN     48
#endif

#define PASV_PORT_START       50000
#define PASV_PORT_END         50009
#define MAX_CLIENTS           2
#define READ_BUF_SIZE_PSRAM   (3 * 1024 * 1024)   // 3 MB read buffer in PSRAM
#define WRITE_BUF_SIZE_PSRAM  (3 * 1024 * 1024)   // 3 MB write buffer in PSRAM
#define BUF_SIZE_IRAM         (16 * 1024)         // 16 KB fallback for internal RAM

size_t   readBufSize  = BUF_SIZE_IRAM;           // actual read buffer size
size_t   writeBufSize = BUF_SIZE_IRAM;           // actual write buffer size

// ─────────────────────────── Runtime Settings (from Preferences) ───────────

bool   useAPMode;
String apSSID, apPassword;
String staSSID, staPassword;
String ftpUser, ftpPass;
String mdnsHost;

// ─────────────────────────── Globals ───────────────────────────────────────

bool      sdMounted = false;
Preferences prefs;
WebServer webServer(80);
WiFiServer ftpServer(21);
SPIClass  sdSPI(FSPI);
uint8_t  *readBuf  = nullptr;
uint8_t  *writeBuf = nullptr;

// ─────────────────────────── FTP State ─────────────────────────────────────

enum FtpState    { FTP_IDLE, FTP_WAIT_USER, FTP_WAIT_PASS, FTP_LOGGED_IN };
enum FtpTransfer { TRANS_NONE, TRANS_LIST, TRANS_NLST, TRANS_RETR, TRANS_STOR };

struct FtpClient {
  WiFiClient  control;
  WiFiClient  data;
  WiFiServer *dataServer;
  FtpState    state;
  FtpTransfer transfer;
  String      cwd;
  String      renamePath;
  File        file;
  bool        typeBinary;
  bool        dataConnActive;
  uint16_t    dataPort;

  void reset() {
    state          = FTP_WAIT_USER;
    transfer       = TRANS_NONE;
    cwd            = "/";
    renamePath     = "";
    typeBinary     = true;
    dataConnActive = false;
    dataPort       = 0;
    if (file)       file.close();
    if (data)       data.stop();
    if (dataServer) {
      dataServer->close();
      delete dataServer;
      dataServer = nullptr;
    }
  }
};

FtpClient clients[MAX_CLIENTS];
uint16_t  nextPasvPort = PASV_PORT_START;

// ─────────────────────────── Web Config Portal ─────────────────────────────

static const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-S3 FTP Config</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;min-height:100vh;padding:20px}
  h1{text-align:center;padding:20px 0;color:#e94560;font-size:1.6em}
  .card{background:#16213e;border-radius:10px;padding:20px;margin:14px auto;max-width:480px;box-shadow:0 4px 15px rgba(0,0,0,.4)}
  h3{color:#0f3460;background:#e94560;padding:8px 12px;border-radius:6px;margin:-20px -20px 16px;font-size:1em}
  label{display:block;margin:12px 0 4px;font-size:.85em;color:#aaa}
  input[type=text],input[type=password]{width:100%;padding:9px;border:1px solid #0f3460;border-radius:5px;background:#0d1b2a;color:#eee;font-size:.95em}
  .row{display:flex;gap:16px;margin:10px 0}
  .row label{display:flex;align-items:center;gap:6px;cursor:pointer;font-size:.95em;color:#ccc}
  .badge{display:inline-block;padding:4px 10px;border-radius:12px;font-size:.8em;font-weight:bold}
  .ok{background:#1b5e20;color:#a5d6a7}.err{background:#b71c1c;color:#ef9a9a}
  button{width:100%;padding:12px;background:#e94560;color:white;border:none;border-radius:6px;font-size:1em;cursor:pointer;margin-top:8px;transition:background .2s}
  button:hover{background:#c73652}
  .info{font-size:.8em;color:#888;margin-top:6px}
  .sep{border:none;border-top:1px solid #0f3460;margin:14px 0}
</style>
</head>
<body>
<h1>🛜 ESP32-S3 FTP Server</h1>

<div class="card">
  <h3>📊 Status</h3>
  <p>SD Card: <span class="badge %SD_CLASS%">%SD_STATUS%</span></p>
  <p style="margin-top:8px">Mode: <span class="badge ok">%WIFI_MODE%</span></p>
  <p style="margin-top:8px">IP: <b>%WIFI_IP%</b> &nbsp;|&nbsp; Hostname: <b>%MDNS_HOST%.local</b></p>
  <p style="margin-top:8px">Memory: <span class="badge ok">%MEM_STATUS%</span></p>
  %SD_DETAIL%
</div>

<form action="/save" method="POST">
<div class="card">
  <h3>📡 WiFi Mode</h3>
  <div class="row">
    <label><input type="radio" name="mode" value="ap" %AP_CHK%> Access Point (AP)</label>
    <label><input type="radio" name="mode" value="sta" %STA_CHK%> Station (join router)</label>
  </div>
  <hr class="sep">
  <b style="color:#e94560;font-size:.85em">ACCESS POINT</b>
  <label>SSID</label>
  <input type="text" name="ap_ssid" value="%AP_SSID%" maxlength="32">
  <label>Password <span class="info">(min 8 characters)</span></label>
  <input type="password" name="ap_pass" value="%AP_PASS%" maxlength="64">
  <hr class="sep">
  <b style="color:#e94560;font-size:.85em">STATION (ROUTER)</b>
  <label>SSID</label>
  <input type="text" name="sta_ssid" value="%STA_SSID%" maxlength="32">
  <label>Password</label>
  <input type="password" name="sta_pass" value="%STA_PASS%" maxlength="64">
</div>

<div class="card">
  <h3>🔐 FTP Credentials</h3>
  <label>Username</label>
  <input type="text" name="ftp_user" value="%FTP_USER%" maxlength="32">
  <label>Password</label>
  <input type="password" name="ftp_pass" value="%FTP_PASS%" maxlength="64">
  <hr class="sep">
  <label>Device Hostname <span class="info">(.local — use instead of IP address)</span></label>
  <input type="text" name="mdns" value="%MDNS_HOST%" maxlength="32" pattern="[a-zA-Z0-9\-]+" title="Letters, numbers and hyphens only">
  <p class="info" style="margin-top:6px">📂 FTP: <b>ftp://%MDNS_HOST%.local</b> &nbsp;|&nbsp; 🌐 Web: <b>http://%MDNS_HOST%.local</b></p>
</div>

<div style="max-width:480px;margin:0 auto">
  <button type="submit">💾 Save &amp; Restart</button>
</div>
</form>

<p style="text-align:center;margin-top:20px;font-size:.75em;color:#555">ESP32-S3 FTP Server &mdash; Arduino Core 3.x</p>
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
<style>body{font-family:Arial,sans-serif;background:#1a1a2e;color:#eee;display:flex;align-items:center;justify-content:center;min-height:100vh;text-align:center}
.box{background:#16213e;padding:40px;border-radius:12px}h2{color:#e94560}p{color:#aaa;margin-top:12px}</style>
</head>
<body>
<div class="box">
  <h2>✅ Settings Saved!</h2>
  <p>Device is restarting…</p>
  <p style="font-size:.8em;margin-top:20px">Redirecting in 4 s</p>
</div>
</body>
</html>
)rawliteral";

// ── Load settings from NVS Preferences ──────────────────────────────────────
void loadPrefs() {
  prefs.begin("ftpcfg", false);
  useAPMode   = prefs.getBool  ("ap_mode",  true);
  apSSID      = prefs.getString("ap_ssid",  "ESP32-FTP");
  apPassword  = prefs.getString("ap_pass",  "12345678");
  staSSID     = prefs.getString("sta_ssid", "");
  staPassword = prefs.getString("sta_pass", "");
  ftpUser     = prefs.getString("ftp_user", "esp32");
  ftpPass     = prefs.getString("ftp_pass", "esp32ftp");
  mdnsHost    = prefs.getString("mdns",     "esp32ftp");
}

// ── Build the config page with live values ───────────────────────────────────
String buildPage() {
  String html = FPSTR(CONFIG_HTML);

  if (sdMounted) {
    html.replace("%SD_CLASS%",  "ok");
    html.replace("%SD_STATUS%", "Mounted ✓");
    String detail = "<p class='info' style='margin-top:8px'>Size: "
                  + String((unsigned long)(SD.cardSize() / (1024*1024))) + " MB &nbsp;|&nbsp; Used: "
                  + String((unsigned long)(SD.usedBytes() / (1024*1024))) + " MB</p>";
    html.replace("%SD_DETAIL%", detail);
  } else {
    html.replace("%SD_CLASS%",  "err");
    html.replace("%SD_STATUS%", "Not Mounted ✗");
    html.replace("%SD_DETAIL%", "<p class='info' style='margin-top:8px;color:#ef9a9a'>Check SD card wiring &amp; format (FAT32/exFAT)</p>");
  }

  html.replace("%WIFI_MODE%", useAPMode ? "Access Point" : "Station");
  html.replace("%WIFI_IP%",   useAPMode
                                ? WiFi.softAPIP().toString()
                                : WiFi.localIP().toString());

  char memBuf[64];
  if (readBufSize >= 1048576) {
    snprintf(memBuf, sizeof(memBuf), "R: %uMB / W: %uMB",
             (unsigned)(readBufSize / 1048576), (unsigned)(writeBufSize / 1048576));
  } else {
    snprintf(memBuf, sizeof(memBuf), "R: %uKB / W: %uKB",
             (unsigned)(readBufSize / 1024), (unsigned)(writeBufSize / 1024));
  }
  String memStatus = String(memBuf);
  if (psramFound()) {
    memStatus += " in PSRAM (" + String((unsigned long)(ESP.getFreePsram() / 1024)) + " KB free)";
  } else {
    memStatus += " in RAM";
  }
  html.replace("%MEM_STATUS%", memStatus);

  html.replace("%AP_CHK%",   useAPMode ? "checked" : "");
  html.replace("%STA_CHK%",  useAPMode ? "" : "checked");
  html.replace("%AP_SSID%",  apSSID);
  html.replace("%AP_PASS%",  apPassword);
  html.replace("%STA_SSID%", staSSID);
  html.replace("%STA_PASS%", staPassword);
  html.replace("%FTP_USER%", ftpUser);
  html.replace("%FTP_PASS%", ftpPass);
  html.replace("%MDNS_HOST%", mdnsHost);
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
  if (webServer.hasArg("ftp_user"))prefs.putString("ftp_user", webServer.arg("ftp_user"));
  if (webServer.hasArg("ftp_pass"))prefs.putString("ftp_pass", webServer.arg("ftp_pass"));
  if (webServer.hasArg("mdns"))    prefs.putString("mdns",     webServer.arg("mdns"));

  Serial.println("[Web] Settings saved — restarting...");
  webServer.send(200, "text/html", FPSTR(SAVED_HTML));
  delay(600);
  ESP.restart();
}

void handleNotFound() {
  webServer.sendHeader("Location", "/", true);
  webServer.send(302, "text/plain", "");
}

// ─────────────────────────── FTP Helpers ───────────────────────────────────

String resolvePath(const String &cwd, const String &arg) {
  String path;
  if (arg.length() == 0) {
    path = cwd;
  } else if (arg.startsWith("/")) {
    path = arg;
  } else {
    path = cwd;
    if (!path.endsWith("/")) path += "/";
    path += arg;
  }

  String parts[32];
  int depth = 0, start = 0, len = path.length();
  for (int i = 0; i <= len; i++) {
    if (i == len || path[i] == '/') {
      String seg = path.substring(start, i);
      start = i + 1;
      if (seg == "" || seg == ".") continue;
      if (seg == "..") { if (depth > 0) depth--; continue; }
      if (depth < 32) parts[depth++] = seg;
    }
  }
  if (depth == 0) return "/";
  String result;
  for (int i = 0; i < depth; i++) result += "/" + parts[i];
  return result;
}

String formatListEntry(File &entry) {
  String name = entry.name();
  int lastSlash = name.lastIndexOf('/');
  if (lastSlash >= 0) name = name.substring(lastSlash + 1);

  char line[280];
  if (entry.isDirectory()) {
    snprintf(line, sizeof(line),
             "drwxr-xr-x 1 esp32 esp32 %10u Jan  1  2025 %s\r\n",
             0, name.c_str());
  } else {
    snprintf(line, sizeof(line),
             "-rw-r--r-- 1 esp32 esp32 %10u Jan  1  2025 %s\r\n",
             (unsigned)entry.size(), name.c_str());
  }
  return String(line);
}

uint16_t allocPasvPort() {
  uint16_t p = nextPasvPort++;
  if (nextPasvPort > PASV_PORT_END) nextPasvPort = PASV_PORT_START;
  return p;
}

// ─────────────────────────── FTP Command Handler ───────────────────────────

void sendReply(FtpClient &c, int code, const char *msg) {
  char buf[256];
  snprintf(buf, sizeof(buf), "%d %s\r\n", code, msg);
  c.control.print(buf);
}
void sendReply(FtpClient &c, int code, const String &msg) {
  sendReply(c, code, msg.c_str());
}

bool openDataConnection(FtpClient &c) {
  if (!c.dataServer) return false;
  unsigned long start = millis();
  while (!c.dataServer->hasClient() && millis() - start < 5000) delay(1);
  if (c.dataServer->hasClient()) {
    c.data = c.dataServer->accept();
    c.dataConnActive = true;
    c.data.setNoDelay(true); // Disable Nagle's algorithm for high TCP throughput
    return true;
  }
  return false;
}

void closeDataConnection(FtpClient &c) {
  if (c.data) c.data.stop();
  if (c.dataServer) {
    c.dataServer->close();
    delete c.dataServer;
    c.dataServer = nullptr;
  }
  c.dataConnActive = false;
}

// Read from SD into buffer in chunks up to 512 KB to avoid TWDT watchdog triggers
static size_t fillBufferFromSD(File &f, uint8_t *buf, size_t len) {
  size_t total = 0;
  while (total < len) {
    size_t chunk = len - total;
    if (chunk > 524288) chunk = 524288;
    int r = f.read(buf + total, chunk);
    if (r <= 0) break;
    total += r;
    yield();
  }
  return total;
}

// Write from buffer to SD in chunks up to 512 KB to avoid TWDT watchdog triggers
static void flushBufferToSD(File &f, const uint8_t *buf, size_t len) {
  size_t written = 0;
  while (written < len) {
    size_t chunk = len - written;
    if (chunk > 524288) chunk = 524288;
    size_t w = f.write(buf + written, chunk);
    if (w == 0) break;
    written += w;
    yield();
  }
}

void handleCommand(FtpClient &c, const String &line) {
  String cmd, arg;
  int sp = line.indexOf(' ');
  if (sp > 0) { cmd = line.substring(0, sp); arg = line.substring(sp + 1); arg.trim(); }
  else         { cmd = line; }
  cmd.toUpperCase();

  // ── Pre-auth ──────────────────────────────────────────────────────────────
  if (cmd == "USER") {
    if (arg == ftpUser) { c.state = FTP_WAIT_PASS; sendReply(c, 331, "Password required"); }
    else                { sendReply(c, 530, "Unknown user"); }
    return;
  }
  if (cmd == "PASS") {
    if (c.state != FTP_WAIT_PASS) { sendReply(c, 503, "Login with USER first"); return; }
    if (arg == ftpPass) {
      c.state = FTP_LOGGED_IN;
      sendReply(c, 230, "Login successful");
      Serial.println("[FTP] Client logged in");
    } else {
      c.state = FTP_WAIT_USER;
      sendReply(c, 530, "Login incorrect");
    }
    return;
  }
  if (cmd == "QUIT") { sendReply(c, 221, "Goodbye"); c.control.stop(); return; }
  if (c.state != FTP_LOGGED_IN) { sendReply(c, 530, "Please login first"); return; }

  // ── System ────────────────────────────────────────────────────────────────
  if      (cmd == "SYST") { sendReply(c, 215, "UNIX Type: L8"); }
  else if (cmd == "FEAT") {
    c.control.print("211-Features:\r\n SIZE\r\n PASV\r\n UTF8\r\n211 End\r\n");
  }
  else if (cmd == "OPTS") {
    sendReply(c, arg.startsWith("UTF8") ? 200 : 501,
              arg.startsWith("UTF8") ? "UTF8 set to on" : "Option not understood");
  }
  else if (cmd == "NOOP") { sendReply(c, 200, "OK"); }
  else if (cmd == "TYPE") {
    if      (arg == "I") { c.typeBinary = true;  sendReply(c, 200, "Type set to I (binary)"); }
    else if (arg == "A") { c.typeBinary = false; sendReply(c, 200, "Type set to A (ASCII)"); }
    else                 { sendReply(c, 504, "Type not supported"); }
  }

  // ── Directory navigation ──────────────────────────────────────────────────
  else if (cmd == "PWD" || cmd == "XPWD") {
    sendReply(c, 257, "\"" + c.cwd + "\" is current directory");
  }
  else if (cmd == "CWD" || cmd == "XCWD") {
    if (!sdMounted) { sendReply(c, 550, "SD card not mounted"); return; }
    String path = resolvePath(c.cwd, arg);
    File dir = SD.open(path);
    if (dir && dir.isDirectory()) {
      c.cwd = path; sendReply(c, 250, "Directory changed to " + c.cwd); dir.close();
    } else {
      sendReply(c, 550, "Directory not found"); if (dir) dir.close();
    }
  }
  else if (cmd == "CDUP" || cmd == "XCUP") {
    c.cwd = resolvePath(c.cwd, ".."); sendReply(c, 250, "Directory changed to " + c.cwd);
  }

  // ── PASV ─────────────────────────────────────────────────────────────────
  else if (cmd == "PASV") {
    if (c.dataServer) { c.dataServer->close(); delete c.dataServer; }
    uint16_t port = allocPasvPort();
    c.dataPort   = port;
    c.dataServer = new WiFiServer(port);
    c.dataServer->begin();

    IPAddress ip = useAPMode ? WiFi.softAPIP() : WiFi.localIP();
    char buf[80];
    snprintf(buf, sizeof(buf),
             "Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
             ip[0], ip[1], ip[2], ip[3], port >> 8, port & 0xFF);
    sendReply(c, 227, buf);
  }

  // ── LIST / NLST ──────────────────────────────────────────────────────────
  else if (cmd == "LIST" || cmd == "NLST") {
    if (!sdMounted) { sendReply(c, 550, "SD card not mounted"); return; }
    String path = resolvePath(c.cwd, arg.startsWith("-") ? "" : arg);

    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
      sendReply(c, 550, "Directory not found"); if (dir) dir.close(); return;
    }

    sendReply(c, 150, "Opening data connection for directory list");
    if (!openDataConnection(c)) {
      sendReply(c, 425, "Cannot open data connection"); dir.close(); return;
    }

    int count = 0;
    File entry = dir.openNextFile();
    while (entry) {
      if (cmd == "LIST") {
        c.data.print(formatListEntry(entry));
      } else {
        String name = entry.name();
        int sl = name.lastIndexOf('/');
        if (sl >= 0) name = name.substring(sl + 1);
        c.data.print(name + "\r\n");
      }
      count++;
      entry.close();
      entry = dir.openNextFile();
    }
    dir.close();
    closeDataConnection(c);
    Serial.printf("[FTP] LIST sent %d entries\n", count);
    sendReply(c, 226, "Directory listing complete");
  }

  // ── SIZE ─────────────────────────────────────────────────────────────────
  else if (cmd == "SIZE") {
    if (!sdMounted) { sendReply(c, 550, "SD card not mounted"); return; }
    String path = resolvePath(c.cwd, arg);
    File f = SD.open(path);
    if (f && !f.isDirectory()) { sendReply(c, 213, String((unsigned long)f.size())); f.close(); }
    else                       { sendReply(c, 550, "File not found"); if (f) f.close(); }
  }

  // ── RETR (download) ──────────────────────────────────────────────────────
  else if (cmd == "RETR") {
    if (!sdMounted) { sendReply(c, 550, "SD card not mounted"); return; }
    String path = resolvePath(c.cwd, arg);
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory()) {
      sendReply(c, 550, "File not found"); if (f) f.close(); return;
    }

    sendReply(c, 150, "Opening data connection for " + arg +
              " (" + String((unsigned long)f.size()) + " bytes)");
    if (!openDataConnection(c)) {
      sendReply(c, 425, "Cannot open data connection"); f.close(); return;
    }

    Serial.printf("[FTP] RETR %s (%lu bytes)\n", path.c_str(), (unsigned long)f.size());
    unsigned long startTime = millis();
    unsigned long sent = 0;

    while (f.available() && c.data.connected()) {
      size_t toRead = f.available();
      if (toRead > readBufSize) toRead = readBufSize;

      // Fast block read from SD into 3MB PSRAM read buffer (in 512KB sub-blocks to allow TWDT yield)
      size_t n = fillBufferFromSD(f, readBuf, toRead);
      if (n == 0) break;

      // Stream buffered data over TCP in up to 64 KB chunks
      size_t written = 0;
      while (written < n && c.data.connected()) {
        size_t chunk = n - written;
        if (chunk > 65536) chunk = 65536; // 64 KB TCP chunk
        size_t w = c.data.write(readBuf + written, chunk);
        if (w == 0) {
          delay(1);
        } else {
          written += w;
        }
        yield();
      }
      sent += written;
      if (!c.data.connected()) break;
    }
    f.close();
    closeDataConnection(c);

    unsigned long elapsed = millis() - startTime;
    float kbps = (elapsed > 0) ? ((float)sent / 1024.0f) / ((float)elapsed / 1000.0f) : 0;
    Serial.printf("[FTP] RETR complete: sent %lu bytes in %lu ms (%.1f KB/s)\n", sent, elapsed, kbps);
    sendReply(c, 226, "Transfer complete");
  }

  // ── STOR (upload) ────────────────────────────────────────────────────────
  else if (cmd == "STOR") {
    if (!sdMounted) { sendReply(c, 550, "SD card not mounted"); return; }
    String path = resolvePath(c.cwd, arg);
    File f = SD.open(path, FILE_WRITE);
    if (!f) { sendReply(c, 550, "Cannot create file"); return; }

    sendReply(c, 150, "Opening data connection for upload");
    if (!openDataConnection(c)) {
      sendReply(c, 425, "Cannot open data connection"); f.close(); return;
    }

    Serial.printf("[FTP] STOR %s\n", path.c_str());
    unsigned long startTime = millis();
    unsigned long received = 0;
    size_t bufFilled = 0;
    unsigned long lastDataTime = millis();

    while (c.data.connected() || c.data.available()) {
      if (c.data.available()) {
        size_t spaceLeft = writeBufSize - bufFilled;
        int avail = c.data.available();
        size_t toRead = (avail < (int)spaceLeft) ? (size_t)avail : spaceLeft;
        int n = c.data.read(writeBuf + bufFilled, toRead);
        if (n > 0) {
          bufFilled += n;
          received += n;
          lastDataTime = millis();
        }

        // When 3MB write buffer is full, flush to SD
        if (bufFilled >= writeBufSize) {
          flushBufferToSD(f, writeBuf, bufFilled);
          bufFilled = 0;
        }
      } else {
        // If data stream paused for > 500ms and we have data buffered, flush to SD
        if (bufFilled > 0 && (millis() - lastDataTime > 500)) {
          flushBufferToSD(f, writeBuf, bufFilled);
          bufFilled = 0;
        }
        delay(1);
      }
      yield();
    }

    // Flush remaining buffer data
    if (bufFilled > 0) {
      flushBufferToSD(f, writeBuf, bufFilled);
      bufFilled = 0;
    }
    f.flush();
    f.close();
    closeDataConnection(c);

    unsigned long elapsed = millis() - startTime;
    float kbps = (elapsed > 0) ? ((float)received / 1024.0f) / ((float)elapsed / 1000.0f) : 0;
    Serial.printf("[FTP] STOR complete: received %lu bytes in %lu ms (%.1f KB/s)\n", received, elapsed, kbps);
    sendReply(c, 226, "Transfer complete");
  }

  // ── DELE ─────────────────────────────────────────────────────────────────
  else if (cmd == "DELE") {
    if (!sdMounted) { sendReply(c, 550, "SD card not mounted"); return; }
    String path = resolvePath(c.cwd, arg);
    sendReply(c, SD.remove(path.c_str()) ? 250 : 550,
              SD.remove(path.c_str()) ? "File deleted" : "Delete failed");
  }

  // ── MKD ──────────────────────────────────────────────────────────────────
  else if (cmd == "MKD" || cmd == "XMKD") {
    if (!sdMounted) { sendReply(c, 550, "SD card not mounted"); return; }
    String path = resolvePath(c.cwd, arg);
    if (SD.mkdir(path.c_str())) sendReply(c, 257, "\"" + path + "\" directory created");
    else                        sendReply(c, 550, "Cannot create directory");
  }

  // ── RMD ──────────────────────────────────────────────────────────────────
  else if (cmd == "RMD" || cmd == "XRMD") {
    if (!sdMounted) { sendReply(c, 550, "SD card not mounted"); return; }
    String path = resolvePath(c.cwd, arg);
    sendReply(c, SD.rmdir(path.c_str()) ? 250 : 550,
              SD.rmdir(path.c_str()) ? "Directory removed" : "Cannot remove directory");
  }

  // ── RNFR / RNTO ──────────────────────────────────────────────────────────
  else if (cmd == "RNFR") {
    if (!sdMounted) { sendReply(c, 550, "SD card not mounted"); return; }
    String path = resolvePath(c.cwd, arg);
    File f = SD.open(path);
    if (f) { c.renamePath = path; f.close(); sendReply(c, 350, "Ready for destination name"); }
    else   { sendReply(c, 550, "File not found"); }
  }
  else if (cmd == "RNTO") {
    if (c.renamePath.length() == 0) { sendReply(c, 503, "RNFR required first"); return; }
    String path = resolvePath(c.cwd, arg);
    sendReply(c, SD.rename(c.renamePath.c_str(), path.c_str()) ? 250 : 550,
              SD.rename(c.renamePath.c_str(), path.c_str()) ? "Renamed OK" : "Rename failed");
    c.renamePath = "";
  }

  else {
    sendReply(c, 502, "Command not implemented");
    Serial.printf("[FTP] Unknown command: %s\n", cmd.c_str());
  }
}

// ─────────────────────────── Setup ─────────────────────────────────────────

void setup() {
  Serial.begin(115200);

  // ── Turn off onboard RGB LED (NeoPixel) ──────────────────────────────────
  neopixelWrite(RGB_BUILTIN, 0, 0, 0);
  neopixelWrite(48, 0, 0, 0); // Standard ESP32-S3 DevKit pin
  neopixelWrite(38, 0, 0, 0); // Alternate board clone pin

  delay(1000);
  Serial.println("\n========================================");
  Serial.println("  ESP32-S3 FTP Server + Web Config");
  Serial.println("========================================\n");

  // ── Load saved settings ──────────────────────────────────────────────────
  loadPrefs();
  Serial.printf("[CFG] Mode: %s\n", useAPMode ? "AP" : "STA");
  Serial.printf("[CFG] FTP user: %s\n", ftpUser.c_str());

  // ── Allocate transfer buffers (prefer PSRAM) ─────────────────────────────
  if (psramFound()) {
    Serial.printf("[MEM] PSRAM found! Initial free PSRAM: %u KB\n", ESP.getFreePsram() / 1024);

    // 1) 3 MB Read Buffer
    readBufSize = READ_BUF_SIZE_PSRAM;
    readBuf = (uint8_t *)ps_malloc(readBufSize);
    while (!readBuf && readBufSize > BUF_SIZE_IRAM) {
      readBufSize /= 2;
      readBuf = (uint8_t *)ps_malloc(readBufSize);
    }

    // 2) 3 MB Write Buffer
    writeBufSize = WRITE_BUF_SIZE_PSRAM;
    writeBuf = (uint8_t *)ps_malloc(writeBufSize);
    while (!writeBuf && writeBufSize > BUF_SIZE_IRAM) {
      writeBufSize /= 2;
      writeBuf = (uint8_t *)ps_malloc(writeBufSize);
    }

    if (readBuf && writeBuf) {
      Serial.printf("[MEM] PSRAM allocated: Read Buf = %u KB (%u MB), Write Buf = %u KB (%u MB)\n",
                    (unsigned)(readBufSize / 1024), (unsigned)(readBufSize / (1024 * 1024)),
                    (unsigned)(writeBufSize / 1024), (unsigned)(writeBufSize / (1024 * 1024)));
    }
  }

  // Fallbacks to internal RAM if PSRAM not found or allocation failed
  if (!readBuf) {
    readBufSize = BUF_SIZE_IRAM;
    readBuf = (uint8_t *)malloc(readBufSize);
    Serial.printf("[MEM] Fallback: Read Buf = %u KB in internal RAM.\n", (unsigned)(readBufSize / 1024));
  }
  if (!writeBuf) {
    writeBufSize = BUF_SIZE_IRAM;
    writeBuf = (uint8_t *)malloc(writeBufSize);
    Serial.printf("[MEM] Fallback: Write Buf = %u KB in internal RAM.\n", (unsigned)(writeBufSize / 1024));
  }

  if (!readBuf || !writeBuf) {
    Serial.println("[MEM] FATAL: cannot allocate read/write buffers!");
    while (1) delay(1000);
  }

  Serial.printf("[MEM] Free PSRAM: %u KB | Free Heap: %u KB\n",
                ESP.getFreePsram() / 1024, ESP.getFreeHeap() / 1024);

  // ── Init SD card ─────────────────────────────────────────────────────────
  Serial.println("[SD]  Initializing SD card...");
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS, sdSPI, 40000000)) {
    Serial.println("[SD]  *** SD card mount FAILED ***");
    Serial.println("      Check wiring and format (FAT32/exFAT).");
    Serial.println("      FTP will work but all file operations will return errors.");
    sdMounted = false;
  } else {
    sdMounted = true;
    uint8_t cardType = SD.cardType();
    const char *types[] = {"NONE", "MMC", "SD", "SDHC", "UNKNOWN"};
    int ti = (cardType <= 3) ? cardType : 4;
    Serial.printf("[SD]  Card type : %s\n", types[ti]);
    Serial.printf("[SD]  Card size : %llu MB\n", SD.cardSize()  / (1024*1024));
    Serial.printf("[SD]  Total     : %llu MB\n", SD.totalBytes()/ (1024*1024));
    Serial.printf("[SD]  Used      : %llu MB\n", SD.usedBytes() / (1024*1024));
  }

  // ── WiFi ─────────────────────────────────────────────────────────────────
  if (useAPMode) {
    Serial.printf("[WiFi] Starting AP: %s\n", apSSID.c_str());
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID.c_str(), apPassword.c_str());
    delay(500);
    Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  } else {
    Serial.printf("[WiFi] Connecting to %s", staSSID.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(staSSID.c_str(), staPassword.c_str());
    int timeout = 40;
    while (WiFi.status() != WL_CONNECTED && timeout-- > 0) { delay(500); Serial.print("."); }
    Serial.println();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] *** Failed to connect! Falling back to AP mode ***");
      useAPMode = true;
      WiFi.mode(WIFI_AP);
      WiFi.softAP(apSSID.c_str(), apPassword.c_str());
      delay(500);
      Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    } else {
      Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    }
  }

  // ── Web server ───────────────────────────────────────────────────────────
  if (MDNS.begin(mdnsHost.c_str())) {
    MDNS.addService("ftp",  "tcp", 21);
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] Hostname: %s.local\n", mdnsHost.c_str());
  } else {
    Serial.println("[mDNS] Failed to start — use IP address instead");
  }

  webServer.on("/",        HTTP_GET,  handleRoot);
  webServer.on("/save",    HTTP_POST, handleSave);
  webServer.onNotFound(handleNotFound);
  webServer.begin();
  Serial.println("[Web] Config portal at http://" +
                 (useAPMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString()));

  // ── FTP server ───────────────────────────────────────────────────────────
  ftpServer.begin();
  for (int i = 0; i < MAX_CLIENTS; i++) { clients[i].dataServer = nullptr; clients[i].reset(); }
  Serial.println("[FTP] Server started on port 21");
  Serial.printf("[FTP] Login: %s / %s\n", ftpUser.c_str(), ftpPass.c_str());
  Serial.println("[FTP] Ready!\n");
}

// ─────────────────────────── Loop ──────────────────────────────────────────

void loop() {
  webServer.handleClient();

  // ── Accept new FTP connections ───────────────────────────────────────────
  if (ftpServer.hasClient()) {
    WiFiClient newClient = ftpServer.accept();
    bool accepted = false;
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (!clients[i].control || !clients[i].control.connected()) {
        clients[i].reset();
        clients[i].control = newClient;
        sendReply(clients[i], 220, "ESP32-S3 FTP Server Ready");
        Serial.printf("[FTP] Client %d connected from %s\n",
                      i, newClient.remoteIP().toString().c_str());
        accepted = true;
        break;
      }
    }
    if (!accepted) {
      newClient.println("421 Server busy\r");
      newClient.stop();
    }
  }

  // ── Process FTP clients ──────────────────────────────────────────────────
  for (int i = 0; i < MAX_CLIENTS; i++) {
    FtpClient &c = clients[i];
    if (!c.control || !c.control.connected()) {
      if (c.control) {
        Serial.printf("[FTP] Client %d disconnected\n", i);
        c.control.stop();
        c.reset();
      }
      continue;
    }

    if (c.control.available()) {
      String line = c.control.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) {
        if (line.startsWith("PASS") || line.startsWith("pass"))
          Serial.printf("[FTP] Client %d -> PASS ****\n", i);
        else
          Serial.printf("[FTP] Client %d -> %s\n", i, line.c_str());
        handleCommand(c, line);
      }
    }
  }

  delay(1);
}
