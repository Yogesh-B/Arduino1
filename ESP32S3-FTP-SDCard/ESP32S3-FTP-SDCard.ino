/*
 * ESP32-S3 FTP Server with MicroSD Card (SPI)
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
 * Dependencies: None beyond ESP32 Arduino Core 3.x (which supports exFAT).
 *
 * Usage:
 *   1. Update WiFi credentials below (or leave AP mode enabled).
 *   2. Upload to ESP32-S3.
 *   3. Connect with any FTP client to the ESP32's IP on port 21.
 *      Default credentials: esp32 / esp32ftp
 */

#include <WiFi.h>
#include <SD.h>
#include <SPI.h>

// ─────────────────────────── Configuration ───────────────────────────

// WiFi — Station mode (connect to your router)
const char *WIFI_SSID     = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Set to true to create an Access Point instead of joining a network
#define USE_AP_MODE false
const char *AP_SSID     = "ESP32-FTP";
const char *AP_PASSWORD = "12345678";

// FTP credentials
const char *FTP_USER = "esp32";
const char *FTP_PASS = "esp32ftp";

// SPI pins for MicroSD card module on ESP32-S3
#define SD_CS   10
#define SD_MOSI 11
#define SD_SCK  12
#define SD_MISO 13

// FTP data port range for PASV mode
#define PASV_PORT_START 50000
#define PASV_PORT_END   50009

// Maximum simultaneous FTP clients
#define MAX_CLIENTS 2

// Transfer buffer size (uses PSRAM if available)
#define BUF_SIZE 4096

// ─────────────────────────── FTP Client State ───────────────────────────

enum FtpState {
  FTP_IDLE,
  FTP_WAIT_USER,
  FTP_WAIT_PASS,
  FTP_LOGGED_IN
};

enum FtpTransfer {
  TRANS_NONE,
  TRANS_LIST,
  TRANS_NLST,
  TRANS_RETR,
  TRANS_STOR
};

struct FtpClient {
  WiFiClient    control;
  WiFiClient    data;
  WiFiServer   *dataServer;
  FtpState      state;
  FtpTransfer   transfer;
  String        cwd;           // current working directory
  String        renamePath;    // for RNFR/RNTO
  File          file;          // open file for RETR/STOR
  bool          typeBinary;
  bool          dataConnActive;
  uint16_t      dataPort;

  void reset() {
    state         = FTP_WAIT_USER;
    transfer      = TRANS_NONE;
    cwd           = "/";
    renamePath    = "";
    typeBinary    = true;
    dataConnActive = false;
    dataPort      = 0;
    if (file) file.close();
    if (data)  data.stop();
    if (dataServer) {
      dataServer->close();
      delete dataServer;
      dataServer = nullptr;
    }
  }
};

// ─────────────────────────── Globals ───────────────────────────

WiFiServer ftpServer(21);
FtpClient  clients[MAX_CLIENTS];
uint16_t   nextPasvPort = PASV_PORT_START;
uint8_t   *transferBuf  = nullptr;

SPIClass sdSPI(FSPI);   // Use FSPI (SPI2) on ESP32-S3

// ─────────────────────────── Helpers ───────────────────────────

// Resolve a path argument relative to the client's CWD
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

  // Normalize: collapse "//", resolve ".."
  String result;
  int len = path.length();
  // Split by '/' and rebuild
  String parts[32];
  int depth = 0;
  int start = 0;
  for (int i = 0; i <= len; i++) {
    if (i == len || path[i] == '/') {
      String seg = path.substring(start, i);
      start = i + 1;
      if (seg == "" || seg == ".") continue;
      if (seg == "..") {
        if (depth > 0) depth--;
        continue;
      }
      if (depth < 32) parts[depth++] = seg;
    }
  }
  if (depth == 0) return "/";
  result = "";
  for (int i = 0; i < depth; i++) {
    result += "/" + parts[i];
  }
  return result;
}

// Format a file entry for LIST (Unix ls -l style)
String formatListEntry(File &entry) {
  String name = entry.name();
  // Strip leading path — just the basename
  int lastSlash = name.lastIndexOf('/');
  if (lastSlash >= 0) name = name.substring(lastSlash + 1);

  char line[256];
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

// Allocate next PASV port (round-robin)
uint16_t allocPasvPort() {
  uint16_t p = nextPasvPort;
  nextPasvPort++;
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
  if (c.dataServer) {
    // PASV mode — wait briefly for client to connect
    unsigned long start = millis();
    while (!c.dataServer->hasClient() && millis() - start < 5000) {
      delay(1);
    }
    if (c.dataServer->hasClient()) {
      c.data = c.dataServer->accept();
      c.dataConnActive = true;
      return true;
    }
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

void handleCommand(FtpClient &c, const String &line) {
  // Parse command and argument
  String cmd, arg;
  int sp = line.indexOf(' ');
  if (sp > 0) {
    cmd = line.substring(0, sp);
    arg = line.substring(sp + 1);
    arg.trim();
  } else {
    cmd = line;
  }
  cmd.toUpperCase();

  // ── Pre-auth commands ──
  if (cmd == "USER") {
    if (arg == FTP_USER) {
      c.state = FTP_WAIT_PASS;
      sendReply(c, 331, "Password required");
    } else {
      sendReply(c, 530, "Unknown user");
    }
    return;
  }

  if (cmd == "PASS") {
    if (c.state != FTP_WAIT_PASS) {
      sendReply(c, 503, "Login with USER first");
      return;
    }
    if (arg == FTP_PASS) {
      c.state = FTP_LOGGED_IN;
      sendReply(c, 230, "Login successful");
      Serial.println("[FTP] Client logged in");
    } else {
      c.state = FTP_WAIT_USER;
      sendReply(c, 530, "Login incorrect");
    }
    return;
  }

  if (cmd == "QUIT") {
    sendReply(c, 221, "Goodbye");
    c.control.stop();
    return;
  }

  // Everything below requires login
  if (c.state != FTP_LOGGED_IN) {
    sendReply(c, 530, "Please login first");
    return;
  }

  // ── System / Feature commands ──
  if (cmd == "SYST") {
    sendReply(c, 215, "UNIX Type: L8");
  }
  else if (cmd == "FEAT") {
    c.control.print("211-Features:\r\n");
    c.control.print(" SIZE\r\n");
    c.control.print(" PASV\r\n");
    c.control.print(" UTF8\r\n");
    c.control.print("211 End\r\n");
  }
  else if (cmd == "OPTS") {
    if (arg.startsWith("UTF8")) {
      sendReply(c, 200, "UTF8 set to on");
    } else {
      sendReply(c, 501, "Option not understood");
    }
  }
  else if (cmd == "NOOP") {
    sendReply(c, 200, "OK");
  }
  else if (cmd == "TYPE") {
    if (arg == "I") {
      c.typeBinary = true;
      sendReply(c, 200, "Type set to I (binary)");
    } else if (arg == "A") {
      c.typeBinary = false;
      sendReply(c, 200, "Type set to A (ASCII)");
    } else {
      sendReply(c, 504, "Type not supported");
    }
  }

  // ── Directory navigation ──
  else if (cmd == "PWD" || cmd == "XPWD") {
    String reply = "\"" + c.cwd + "\" is current directory";
    sendReply(c, 257, reply);
  }
  else if (cmd == "CWD" || cmd == "XCWD") {
    String path = resolvePath(c.cwd, arg);
    File dir = SD.open(path);
    if (dir && dir.isDirectory()) {
      c.cwd = path;
      sendReply(c, 250, "Directory changed to " + c.cwd);
      dir.close();
    } else {
      sendReply(c, 550, "Directory not found");
      if (dir) dir.close();
    }
  }
  else if (cmd == "CDUP" || cmd == "XCUP") {
    c.cwd = resolvePath(c.cwd, "..");
    sendReply(c, 250, "Directory changed to " + c.cwd);
  }

  // ── PASV mode ──
  else if (cmd == "PASV") {
    // Close any existing data listener
    if (c.dataServer) {
      c.dataServer->close();
      delete c.dataServer;
    }
    uint16_t port = allocPasvPort();
    c.dataPort = port;
    c.dataServer = new WiFiServer(port);
    c.dataServer->begin();

    IPAddress ip = WiFi.localIP();
    if (USE_AP_MODE) ip = WiFi.softAPIP();

    char buf[80];
    snprintf(buf, sizeof(buf),
             "Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
             ip[0], ip[1], ip[2], ip[3],
             port >> 8, port & 0xFF);
    sendReply(c, 227, buf);
  }

  // ── LIST / NLST ──
  else if (cmd == "LIST" || cmd == "NLST") {
    String path = resolvePath(c.cwd, arg);
    // Remove -la or similar flags from arg
    if (path.startsWith("-")) path = c.cwd;

    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
      sendReply(c, 550, "Directory not found");
      if (dir) dir.close();
      return;
    }

    sendReply(c, 150, "Opening data connection for directory list");

    if (!openDataConnection(c)) {
      sendReply(c, 425, "Cannot open data connection");
      dir.close();
      return;
    }

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
      entry.close();
      entry = dir.openNextFile();
    }
    dir.close();

    closeDataConnection(c);
    sendReply(c, 226, "Directory listing complete");
  }

  // ── SIZE ──
  else if (cmd == "SIZE") {
    String path = resolvePath(c.cwd, arg);
    File f = SD.open(path);
    if (f && !f.isDirectory()) {
      sendReply(c, 213, String((unsigned long)f.size()));
      f.close();
    } else {
      sendReply(c, 550, "File not found");
      if (f) f.close();
    }
  }

  // ── RETR (download) ──
  else if (cmd == "RETR") {
    String path = resolvePath(c.cwd, arg);
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory()) {
      sendReply(c, 550, "File not found");
      if (f) f.close();
      return;
    }

    sendReply(c, 150, "Opening data connection for " + arg +
              " (" + String((unsigned long)f.size()) + " bytes)");

    if (!openDataConnection(c)) {
      sendReply(c, 425, "Cannot open data connection");
      f.close();
      return;
    }

    Serial.printf("[FTP] RETR %s (%lu bytes)\n", path.c_str(),
                  (unsigned long)f.size());

    unsigned long sent = 0;
    while (f.available()) {
      int n = f.read(transferBuf, BUF_SIZE);
      if (n <= 0) break;
      c.data.write(transferBuf, n);
      sent += n;
      yield();   // feed watchdog
    }
    f.close();
    closeDataConnection(c);

    Serial.printf("[FTP] Sent %lu bytes\n", sent);
    sendReply(c, 226, "Transfer complete");
  }

  // ── STOR (upload) ──
  else if (cmd == "STOR") {
    String path = resolvePath(c.cwd, arg);
    File f = SD.open(path, FILE_WRITE);
    if (!f) {
      sendReply(c, 550, "Cannot create file");
      return;
    }

    sendReply(c, 150, "Opening data connection for upload");

    if (!openDataConnection(c)) {
      sendReply(c, 425, "Cannot open data connection");
      f.close();
      return;
    }

    Serial.printf("[FTP] STOR %s\n", path.c_str());

    unsigned long received = 0;
    while (c.data.connected() || c.data.available()) {
      if (c.data.available()) {
        int n = c.data.read(transferBuf, BUF_SIZE);
        if (n > 0) {
          f.write(transferBuf, n);
          received += n;
        }
      } else {
        delay(1);
      }
      yield();
    }
    f.close();
    closeDataConnection(c);

    Serial.printf("[FTP] Received %lu bytes\n", received);
    sendReply(c, 226, "Transfer complete");
  }

  // ── DELE (delete file) ──
  else if (cmd == "DELE") {
    String path = resolvePath(c.cwd, arg);
    if (SD.remove(path.c_str())) {
      sendReply(c, 250, "File deleted");
    } else {
      sendReply(c, 550, "Delete failed");
    }
  }

  // ── MKD (make directory) ──
  else if (cmd == "MKD" || cmd == "XMKD") {
    String path = resolvePath(c.cwd, arg);
    if (SD.mkdir(path.c_str())) {
      sendReply(c, 257, "\"" + path + "\" directory created");
    } else {
      sendReply(c, 550, "Cannot create directory");
    }
  }

  // ── RMD (remove directory) ──
  else if (cmd == "RMD" || cmd == "XRMD") {
    String path = resolvePath(c.cwd, arg);
    if (SD.rmdir(path.c_str())) {
      sendReply(c, 250, "Directory removed");
    } else {
      sendReply(c, 550, "Cannot remove directory");
    }
  }

  // ── RNFR / RNTO (rename) ──
  else if (cmd == "RNFR") {
    String path = resolvePath(c.cwd, arg);
    File f = SD.open(path);
    if (f) {
      c.renamePath = path;
      f.close();
      sendReply(c, 350, "Ready for destination name");
    } else {
      sendReply(c, 550, "File not found");
    }
  }
  else if (cmd == "RNTO") {
    if (c.renamePath.length() == 0) {
      sendReply(c, 503, "RNFR required first");
      return;
    }
    String path = resolvePath(c.cwd, arg);
    if (SD.rename(c.renamePath.c_str(), path.c_str())) {
      sendReply(c, 250, "Renamed OK");
    } else {
      sendReply(c, 550, "Rename failed");
    }
    c.renamePath = "";
  }

  // ── Unsupported ──
  else {
    sendReply(c, 502, "Command not implemented");
    Serial.printf("[FTP] Unknown command: %s\n", cmd.c_str());
  }
}

// ─────────────────────────── Setup ───────────────────────────

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n========================================");
  Serial.println("  ESP32-S3 FTP Server + MicroSD Card");
  Serial.println("========================================\n");

  // ── Allocate transfer buffer (prefer PSRAM) ──
  if (psramFound()) {
    transferBuf = (uint8_t *)ps_malloc(BUF_SIZE);
    Serial.printf("[MEM] PSRAM found: %u KB free. Buffer in PSRAM.\n",
                  ESP.getFreePsram() / 1024);
  } else {
    transferBuf = (uint8_t *)malloc(BUF_SIZE);
    Serial.println("[MEM] No PSRAM, using internal RAM for buffer.");
  }
  if (!transferBuf) {
    Serial.println("[MEM] FATAL: cannot allocate transfer buffer!");
    while (1) delay(1000);
  }

  // ── Init SD card over SPI ──
  Serial.println("[SD]  Initializing SD card...");
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS, sdSPI, 40000000)) {  // 40 MHz SPI clock
    Serial.println("[SD]  *** SD card mount FAILED! ***");
    Serial.println("      Check wiring and card format (FAT32/exFAT).");
    Serial.println("      ESP32 Arduino Core 3.x supports exFAT.");
    while (1) delay(1000);
  }

  uint8_t cardType = SD.cardType();
  const char *types[] = {"NONE", "MMC", "SD", "SDHC", "UNKNOWN"};
  int ti = (cardType <= 3) ? cardType : 4;
  Serial.printf("[SD]  Card type: %s\n", types[ti]);
  Serial.printf("[SD]  Card size: %llu MB\n", SD.cardSize() / (1024 * 1024));
  Serial.printf("[SD]  Total:     %llu MB\n", SD.totalBytes() / (1024 * 1024));
  Serial.printf("[SD]  Used:      %llu MB\n", SD.usedBytes() / (1024 * 1024));

  // ── WiFi ──
  if (USE_AP_MODE) {
    Serial.printf("[WiFi] Starting Access Point: %s\n", AP_SSID);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  } else {
    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int timeout = 40;  // 20 seconds
    while (WiFi.status() != WL_CONNECTED && timeout-- > 0) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] *** Connection FAILED! Restarting... ***");
      ESP.restart();
    }
    Serial.printf("[WiFi] Connected! IP: %s\n",
                  WiFi.localIP().toString().c_str());
  }

  // ── Start FTP server ──
  ftpServer.begin();
  for (int i = 0; i < MAX_CLIENTS; i++) {
    clients[i].dataServer = nullptr;
    clients[i].reset();
  }
  Serial.println("\n[FTP] Server started on port 21");
  Serial.printf("[FTP] Login: %s / %s\n", FTP_USER, FTP_PASS);
  Serial.println("[FTP] Ready for connections!\n");
}

// ─────────────────────────── Loop ───────────────────────────

void loop() {
  // ── Accept new control connections ──
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
      newClient.println("421 Server busy, max clients reached\r\n");
      newClient.stop();
    }
  }

  // ── Process each client ──
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

    // Read and process commands (one line at a time)
    if (c.control.available()) {
      String line = c.control.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) {
        // Don't log passwords
        if (line.startsWith("PASS") || line.startsWith("pass")) {
          Serial.printf("[FTP] Client %d -> PASS ****\n", i);
        } else {
          Serial.printf("[FTP] Client %d -> %s\n", i, line.c_str());
        }
        handleCommand(c, line);
      }
    }
  }

  delay(1);  // small yield
}

