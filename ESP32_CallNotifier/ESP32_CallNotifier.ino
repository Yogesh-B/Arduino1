/*
 * ESP32-S3 BLE Call Notifier — Single File Arduino Sketch (v2.0)
 * ─────────────────────────────────────────────────────────────
 * Tested on: ESP32-S3 Dev Module / ESP32-S3-WROOM-1 / DevKitC-1
 * Arduino ESP32 Core: v3.x (ESP-IDF 5.x)
 *
 * Overview:
 *   ESP32-S3 hardware does NOT support Classic Bluetooth (BR/EDR / HFP).
 *   Instead, this version uses Bluetooth Low Energy (BLE) implementing the
 *   standard Nordic UART Service (NUS) compatible with Gadgetbridge (Android).
 *
 * How It Works:
 *   1. Starts WiFi SoftAP ("ESP32_CallNotifier" / pass: "12345678") at 192.168.4.1.
 *   2. Connects to user-configured WiFi network in AP+STA mode (without dropping AP).
 *   3. Runs BLE GATT Server advertising as "Bangle.js CallNotifier" with Nordic UART Service.
 *   4. Android phone runs Gadgetbridge (free & open-source on F-Droid / Google Play).
 *   5. When an incoming phone call occurs, Gadgetbridge forwards call alerts over BLE.
 *   6. Web dashboard receives real-time Server-Sent Events (SSE) and triggers:
 *      - Browser Desktop Notifications API.
 *      - Offline Web Audio ringtone synthesis (realistic dual-frequency telephone bell).
 *      - Full-screen incoming call visual modal banner with Caller ID & Contact name.
 *      - Local phonebook/contacts mapping saved in browser localStorage.
 *      - Recent call history log.
 *
 * Gadgetbridge Setup (Quick 1-Minute Setup):
 *   1. Install Gadgetbridge on your Android phone (free on F-Droid: https://f-droid.org/packages/nodomain.freeyourgadget.gadgetbridge/
 *      or GitHub: https://github.com/Freeyourgadget/Gadgetbridge).
 *   2. Open Gadgetbridge, grant Notification & Phone permissions.
 *   3. Tap the '+' (Add device) button to scan for nearby devices.
 *   4. Select "Bangle.js CallNotifier" from the list and tap "Pair / Connect".
 *   5. Done! Calls and app notifications will now trigger your ESP32 web dashboard in real time.
 *
 * Board Settings in Arduino IDE:
 *   - Board: "ESP32S3 Dev Module"
 *   - USB CDC On Boot: "Enabled" (if using native USB)
 *   - Flash Size: "4MB" or "8MB" (depending on your module)
 *   - Partition Scheme: "Default 4MB with spiffs" or "Minimal SPIFFS"
 *   - Upload Speed: 921600
 */

#include <WiFi.h>
#include <Preferences.h>
#include "esp_wifi.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "webpage.h"

// ── Configuration Constants ──────────────────────────────────────────────────
#define AP_SSID_DEFAULT     "ESP32_CallNotifier"
#define AP_PASS_DEFAULT     "12345678"
#define MAX_SSE_CLIENTS     8
#define HTTP_PORT           80

// Nordic UART Service (NUS) UUIDs recognized by Gadgetbridge / Bangle.js
#define NUS_SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_CHARACTERISTIC_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e" // Phone writes to ESP32
#define NUS_CHARACTERISTIC_TX_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e" // ESP32 notifies Phone
#define BLE_DEVICE_NAME            "Bangle.js CallNotifier"

// ── Global State Variables ───────────────────────────────────────────────────
Preferences prefs;

// WiFi state
String sta_ssid = "";
String sta_password = "";
bool sta_connected = false;
int ap_client_count = 0;

// BLE state
bool ble_initialized = false;
bool ble_connected = false;
String ble_client_addr = "";
BLEServer* pBleServer = nullptr;
BLECharacteristic* pTxCharacteristic = nullptr;
BLECharacteristic* pRxCharacteristic = nullptr;
String ble_rx_accumulator = "";

// Call State
bool call_ringing = false;
bool call_active = false;
String current_caller_number = "";
String current_caller_name = "";
String last_caller_number = "";
String last_caller_name = "";
unsigned long call_start_millis = 0;
unsigned long last_call_timestamp = 0;

// Generic Notification State (optional extra notifications from Android)
String last_notif_src = "";
String last_notif_title = "";
String last_notif_body = "";
unsigned long last_notif_time = 0;

// SSE & Web Server
WiFiServer server(HTTP_PORT);
WiFiClient sseClients[MAX_SSE_CLIENTS];
SemaphoreHandle_t sseMutex = NULL;
unsigned long lastSsePing = 0;

// ── SSE Broadcast Helper ─────────────────────────────────────────────────────
void broadcastSSE(const char* event, const String& data) {
    if (!sseMutex) return;
    if (xSemaphoreTake(sseMutex, pdMS_TO_TICKS(150)) == pdTRUE) {
        for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
            if (sseClients[i] && sseClients[i].connected()) {
                sseClients[i].printf("event: %s\ndata: %s\n\n", event, data.c_str());
                sseClients[i].flush();
            }
        }
        xSemaphoreGive(sseMutex);
    }
}

// ── NVS Configuration Helpers ────────────────────────────────────────────────
void loadConfig() {
    prefs.begin("callnotif", true);
    sta_ssid = prefs.getString("sta_ssid", "");
    sta_password = prefs.getString("sta_pwd", "");
    prefs.end();

    if (sta_ssid.length() > 0) {
        Serial.printf("[NVS] Loaded saved WiFi: %s\n", sta_ssid.c_str());
    }
}

void saveWifiConfig(const String& ssid, const String& pwd) {
    prefs.begin("callnotif", false);
    prefs.putString("sta_ssid", ssid);
    prefs.putString("sta_pwd", pwd);
    prefs.end();
    sta_ssid = ssid;
    sta_password = pwd;
    Serial.printf("[NVS] Saved WiFi SSID: %s\n", ssid.c_str());
}

// ── Lightweight JSON Helper (Zero External Libraries) ────────────────────────
String extractJsonString(const String& json, const String& key) {
    String searchKey = "\"" + key + "\"";
    int pos = json.indexOf(searchKey);
    if (pos == -1) {
        searchKey = key + ":";
        pos = json.indexOf(searchKey);
        if (pos == -1) return "";
        pos += searchKey.length();
    } else {
        pos += searchKey.length();
        pos = json.indexOf(':', pos);
        if (pos == -1) return "";
        pos++;
    }

    while (pos < (int)json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= (int)json.length()) return "";

    if (json[pos] == '\"') {
        pos++;
        int endPos = json.indexOf('\"', pos);
        if (endPos == -1) return "";
        return json.substring(pos, endPos);
    } else {
        int endPos = pos;
        while (endPos < (int)json.length() && 
               json[endPos] != ',' && json[endPos] != '}' && 
               json[endPos] != ')' && json[endPos] != '\n' && json[endPos] != '\r') {
            endPos++;
        }
        String val = json.substring(pos, endPos);
        val.trim();
        return val;
    }
}

// ── Gadgetbridge Protocol Parser ─────────────────────────────────────────────
void handleGadgetMessage(const String& raw) {
    String msg = raw;
    msg.trim();
    if (msg.length() == 0) return;

    Serial.printf("[BLE RX] << %s\n", msg.c_str());

    // Extract JSON inside GB(...) or standalone {...}
    String json = msg;
    int gbStart = msg.indexOf("GB(");
    if (gbStart != -1) {
        int firstBrace = msg.indexOf('{', gbStart);
        int lastBrace = msg.lastIndexOf('}');
        if (firstBrace != -1 && lastBrace != -1 && lastBrace > firstBrace) {
            json = msg.substring(firstBrace, lastBrace + 1);
        }
    }

    String type = extractJsonString(json, "t");

    // ── 1. Phone Call Events ──
    if (type == "call") {
        String cmd = extractJsonString(json, "cmd");

        if (cmd == "incoming") {
            String name = extractJsonString(json, "name");
            String number = extractJsonString(json, "number");
            if (number.length() == 0 && name.length() > 0) {
                number = name;
            }

            current_caller_number = number;
            current_caller_name = name;
            last_caller_number = number;
            last_caller_name = name;
            call_ringing = true;
            call_active = false;
            call_start_millis = millis();
            last_call_timestamp = millis();

            Serial.printf("\n🔔 *** INCOMING PHONE CALL *** 🔔\n");
            Serial.printf("Caller Name: %s | Number: %s\n\n", name.c_str(), number.c_str());

            String sseData = "{\"number\":\"" + number + "\",\"name\":\"" + name + "\"}";
            broadcastSSE("incoming_call", sseData);
        }
        else if (cmd == "start" || cmd == "accept") {
            call_ringing = false;
            call_active = true;
            Serial.println("[HFP/BLE] Call Answered / Active");
            String sseData = "{\"number\":\"" + current_caller_number + "\",\"name\":\"" + current_caller_name + "\"}";
            broadcastSSE("call_answered", sseData);
        }
        else if (cmd == "end" || cmd == "reject" || cmd == "dismiss") {
            Serial.println("[HFP/BLE] Call Ended / Rejected");
            call_ringing = false;
            call_active = false;
            broadcastSSE("call_ended", "{}");
            current_caller_number = "";
            current_caller_name = "";
        }
    }
    // ── 2. Generic App Notifications (WhatsApp, SMS, etc.) ──
    else if (type == "notify") {
        String src = extractJsonString(json, "src");
        String title = extractJsonString(json, "title");
        String body = extractJsonString(json, "body");

        last_notif_src = src;
        last_notif_title = title;
        last_notif_body = body;
        last_notif_time = millis();

        Serial.printf("[NOTIF] From: %s | Title: %s | Body: %s\n", src.c_str(), title.c_str(), body.c_str());

        // Escape quotes for JSON broadcast
        String safeTitle = title;
        safeTitle.replace("\"", "\\\"");
        String safeBody = body;
        safeBody.replace("\"", "\\\"");
        String safeSrc = src;
        safeSrc.replace("\"", "\\\"");

        String sseData = "{\"src\":\"" + safeSrc + "\",\"title\":\"" + safeTitle + "\",\"body\":\"" + safeBody + "\"}";
        broadcastSSE("phone_notification", sseData);
    }
}

// ── BLE Server Callbacks ─────────────────────────────────────────────────────
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        ble_connected = true;
        Serial.println("[BLE] Client connected (Gadgetbridge / Android Phone)!");
        String json = "{\"state\":\"connected\",\"device\":\"" + String(BLE_DEVICE_NAME) + "\"}";
        broadcastSSE("ble_status", json);
    }

    void onDisconnect(BLEServer* pServer) override {
        ble_connected = false;
        call_ringing = false;
        call_active = false;
        Serial.println("[BLE] Client disconnected. Restarting BLE advertising...");
        String json = "{\"state\":\"disconnected\",\"device\":\"\"}";
        broadcastSSE("ble_status", json);
        BLEDevice::startAdvertising();
    }
};

// ── BLE Characteristic RX Callback (Receives data from Gadgetbridge) ────────
class MyRxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        String rxValue = pCharacteristic->getValue();
        if (rxValue.length() == 0) return;

        for (size_t i = 0; i < rxValue.length(); i++) {
            char c = rxValue[i];
            if (c == '\n' || c == '\r') {
                if (ble_rx_accumulator.length() > 0) {
                    handleGadgetMessage(ble_rx_accumulator);
                    ble_rx_accumulator = "";
                }
            } else {
                ble_rx_accumulator += c;
                // Safeguard buffer size
                if (ble_rx_accumulator.length() > 1024) {
                    ble_rx_accumulator = "";
                }
            }
        }

        // If buffer looks like a complete GB(...) call ending with ')', process immediately
        if (ble_rx_accumulator.startsWith("GB(") && ble_rx_accumulator.endsWith(")")) {
            handleGadgetMessage(ble_rx_accumulator);
            ble_rx_accumulator = "";
        }
    }
};

// ── Bluetooth Stack Initialization ───────────────────────────────────────────
bool initBluetooth() {
    Serial.println("[BLE] Initializing Bluetooth Low Energy (BLE) subsystem on ESP32-S3...");

    BLEDevice::init(BLE_DEVICE_NAME);
    BLEDevice::setMTU(512);

    pBleServer = BLEDevice::createServer();
    pBleServer->setCallbacks(new MyServerCallbacks());

    // Create Nordic UART Service
    BLEService *pService = pBleServer->createService(NUS_SERVICE_UUID);

    // TX Characteristic (ESP32 -> Phone via Notification)
    pTxCharacteristic = pService->createCharacteristic(
        NUS_CHARACTERISTIC_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pTxCharacteristic->addDescriptor(new BLE2902());

    // RX Characteristic (Phone -> ESP32 via Write)
    pRxCharacteristic = pService->createCharacteristic(
        NUS_CHARACTERISTIC_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    pRxCharacteristic->setCallbacks(new MyRxCallbacks());

    pService->start();

    // Start Advertising with Nordic UART Service UUID
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(NUS_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06); // functions that help with iPhone / Android connections
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    ble_initialized = true;
    Serial.printf("[BLE] Advertising started as '%s'\n", BLE_DEVICE_NAME);
    Serial.println("[BLE] Waiting for Gadgetbridge to connect...");
    return true;
}

// ── WiFi Event Handler ───────────────────────────────────────────────────────
static void onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            sta_connected = true;
            Serial.printf("[WiFi] STA connected! IP: %s | GW: %s\n",
                          WiFi.localIP().toString().c_str(),
                          WiFi.gatewayIP().toString().c_str());
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            sta_connected = false;
            Serial.println("[WiFi] STA disconnected");
            if (sta_ssid.length() > 0) {
                WiFi.begin(sta_ssid.c_str(), sta_password.c_str());
            }
            break;

        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            ap_client_count++;
            Serial.printf("[WiFi] Client connected to SoftAP (total: %d)\n", ap_client_count);
            break;

        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            if (ap_client_count > 0) ap_client_count--;
            Serial.printf("[WiFi] Client disconnected from SoftAP (total: %d)\n", ap_client_count);
            break;

        default:
            break;
    }
}

// ── URL & JSON Helper Functions ──────────────────────────────────────────────
String urlDecode(const String& str) {
    String decoded = "";
    char a, b;
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '%') {
            if (i + 2 < str.length()) {
                a = str[i + 1];
                b = str[i + 2];
                if (isxdigit(a) && isxdigit(b)) {
                    a = (a >= 'A' && a <= 'F') ? a - 'A' + 10 : (a >= 'a' && a <= 'f') ? a - 'a' + 10 : a - '0';
                    b = (b >= 'A' && b <= 'F') ? b - 'A' + 10 : (b >= 'a' && b <= 'f') ? b - 'a' + 10 : b - '0';
                    decoded += (char)(16 * a + b);
                    i += 2;
                    continue;
                }
            }
        } else if (str[i] == '+') {
            decoded += ' ';
        } else {
            decoded += str[i];
        }
    }
    return decoded;
}

String getParam(const String& data, const String& key) {
    int start = data.indexOf(key + "=");
    if (start == -1) return "";
    start += key.length() + 1;
    int end = data.indexOf('&', start);
    if (end == -1) end = data.length();
    return urlDecode(data.substring(start, end));
}

// ── HTTP Request Handler ─────────────────────────────────────────────────────
void handleHttpRequest(WiFiClient& client) {
    unsigned long startWait = millis();
    while (!client.available() && millis() - startWait < 500) {
        delay(5);
    }
    if (!client.available()) {
        client.stop();
        return;
    }

    String reqLine = client.readStringUntil('\r');
    client.readStringUntil('\n');

    int s1 = reqLine.indexOf(' ');
    int s2 = reqLine.indexOf(' ', s1 + 1);
    if (s1 == -1 || s2 == -1) {
        client.stop();
        return;
    }

    String method = reqLine.substring(0, s1);
    String fullPath = reqLine.substring(s1 + 1, s2);
    String path = fullPath;
    String query = "";
    int qPos = fullPath.indexOf('?');
    if (qPos != -1) {
        path = fullPath.substring(0, qPos);
        query = fullPath.substring(qPos + 1);
    }

    // Read headers
    int contentLength = 0;
    while (client.available()) {
        String h = client.readStringUntil('\r');
        client.readStringUntil('\n');
        if (h.length() == 0) break;
        if (h.startsWith("Content-Length: ") || h.startsWith("content-length: ")) {
            contentLength = h.substring(16).toInt();
        }
    }

    // Read POST body if present
    String body = "";
    if (contentLength > 0 && contentLength < 4096) {
        unsigned long bStart = millis();
        while (body.length() < (size_t)contentLength && millis() - bStart < 1000) {
            while (client.available() && body.length() < (size_t)contentLength) {
                body += (char)client.read();
            }
            delay(2);
        }
    }

    // Route 1: Main HTML Application
    if (method == "GET" && (path == "/" || path == "/index.html")) {
        client.print("HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/html; charset=utf-8\r\n"
                     "Connection: close\r\n\r\n");
        client.print(FPSTR(HTML_PAGE));
        client.stop();
        return;
    }

    // Route 2: SSE Stream (/events)
    if (method == "GET" && path == "/events") {
        if (xSemaphoreTake(sseMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            int slot = -1;
            for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
                if (!sseClients[i] || !sseClients[i].connected()) {
                    slot = i;
                    break;
                }
            }
            if (slot != -1) {
                client.print("HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/event-stream\r\n"
                             "Cache-Control: no-cache\r\n"
                             "Connection: keep-alive\r\n"
                             "Access-Control-Allow-Origin: *\r\n\r\n");
                client.flush();

                // Send current initial BLE status
                String statusData = "{\"state\":\"" + String(ble_connected ? "connected" : "advertising") + 
                                    "\",\"device\":\"" + String(BLE_DEVICE_NAME) + "\"}";
                client.printf("event: ble_status\ndata: %s\n\n", statusData.c_str());
                client.flush();

                sseClients[slot] = client;
                Serial.printf("[SSE] Client connected in slot %d\n", slot);
            } else {
                client.print("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\nToo many SSE clients");
                client.stop();
            }
            xSemaphoreGive(sseMutex);
        } else {
            client.stop();
        }
        return;
    }

    // Route 3: System Status (/status)
    if (method == "GET" && path == "/status") {
        String json = "{";
        json += "\"wifi\":{";
        json += "\"sta_connected\":" + String(sta_connected ? "true" : "false") + ",";
        json += "\"sta_ssid\":\"" + sta_ssid + "\",";
        json += "\"sta_ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"sta_gw\":\"" + WiFi.gatewayIP().toString() + "\",";
        json += "\"sta_rssi\":" + String(WiFi.RSSI()) + ",";
        json += "\"ap_ssid\":\"" + String(AP_SSID_DEFAULT) + "\",";
        json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
        json += "\"ap_clients\":" + String(ap_client_count);
        json += "},";

        json += "\"ble\":{";
        json += "\"initialized\":" + String(ble_initialized ? "true" : "false") + ",";
        json += "\"state\":\"" + String(ble_connected ? "connected" : "advertising") + "\",";
        json += "\"device\":\"" + String(BLE_DEVICE_NAME) + "\"";
        json += "},";

        json += "\"call\":{";
        json += "\"ringing\":" + String(call_ringing ? "true" : "false") + ",";
        json += "\"active\":" + String(call_active ? "true" : "false") + ",";
        json += "\"number\":\"" + current_caller_number + "\",";
        json += "\"name\":\"" + current_caller_name + "\",";
        json += "\"last_number\":\"" + last_caller_number + "\",";
        json += "\"last_name\":\"" + last_caller_name + "\",";
        json += "\"last_time_ms\":" + String(last_call_timestamp);
        json += "},";

        json += "\"uptime_sec\":" + String(millis() / 1000);
        json += "}";

        client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n");
        client.print(json);
        client.stop();
        return;
    }

    // Route 4: Save WiFi Credentials (/save-wifi)
    if (method == "POST" && path == "/save-wifi") {
        String data = body.length() > 0 ? body : query;
        String s = getParam(data, "ssid");
        String p = getParam(data, "pwd");
        if (s.length() > 0) {
            saveWifiConfig(s, p);
            WiFi.begin(s.c_str(), p.c_str());
            client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n");
            client.print("{\"success\":true,\"message\":\"Connecting to WiFi...\"}");
        } else {
            client.print("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n{\"error\":\"SSID required\"}");
        }
        client.stop();
        return;
    }

    // Default 404
    client.print("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\nNot Found");
    client.stop();
}

// ── Setup Function ───────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n==============================================");
    Serial.println("    ESP32-S3 BLE Call Notifier v2.0           ");
    Serial.println("==============================================");

    // 1. Create Mutex for SSE clients
    sseMutex = xSemaphoreCreateMutex();

    // 2. Load stored NVS configuration
    loadConfig();

    // 3. Initialize WiFi in AP+STA mode
    WiFi.onEvent(onWiFiEvent);
    WiFi.mode(WIFI_AP_STA);

    // Start SoftAP
    bool apStarted = WiFi.softAP(AP_SSID_DEFAULT, AP_PASS_DEFAULT);
    if (apStarted) {
        Serial.printf("[WiFi] SoftAP started successfully: '%s'\n", AP_SSID_DEFAULT);
        Serial.printf("[WiFi] SoftAP IP: %s\n", WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("[WiFi] ERROR: Failed to start SoftAP");
    }

    // Disable WiFi power save for snappy web response and BLE coexistence
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Auto-connect to saved WiFi station network
    if (sta_ssid.length() > 0) {
        Serial.printf("[WiFi] Auto-connecting to saved AP: '%s'...\n", sta_ssid.c_str());
        WiFi.begin(sta_ssid.c_str(), sta_password.c_str());
    }

    // 4. Initialize Bluetooth Low Energy (BLE) Nordic UART Service
    if (!initBluetooth()) {
        Serial.println("[BLE] WARNING: BLE initialization reported issues.");
    }

    // 5. Start Web Server
    server.begin();
    Serial.println("[HTTP] Web server listening on port 80");
    Serial.println("[READY] Open http://192.168.4.1 in your browser to view dashboard.\n");
}

// ── Main Loop ────────────────────────────────────────────────────────────────
void loop() {
    // 1. Handle incoming HTTP client connections
    WiFiClient client = server.accept();
    if (client) {
        handleHttpRequest(client);
    }

    unsigned long now = millis();

    // 2. Manage SSE clients: clean up disconnected and send periodic pings
    if (now - lastSsePing >= 20000) {
        lastSsePing = now;
        if (xSemaphoreTake(sseMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
                if (sseClients[i]) {
                    if (sseClients[i].connected()) {
                        sseClients[i].print(": ping\n\n");
                        sseClients[i].flush();
                    } else {
                        sseClients[i].stop();
                    }
                }
            }
            xSemaphoreGive(sseMutex);
        }
    }

    delay(2); // Yield to FreeRTOS watchdog & background tasks
}
