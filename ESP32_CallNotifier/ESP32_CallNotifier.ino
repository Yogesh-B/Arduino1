/*
 * ESP32 HFP Call Notifier — Single File Arduino Sketch
 * ─────────────────────────────────────────────────────────────
 * Tested on: ESP32-WROOM-32 / ESP32-DevKitC (Original ESP32)
 *
 * Overview:
 *   1. Starts WiFi SoftAP ("ESP32_CallNotifier" / pass: "12345678") at 192.168.4.1.
 *   2. Connects to user-configured WiFi network in AP+STA mode (without dropping AP).
 *   3. Runs Classic Bluetooth HFP (Hands-Free Profile) Client.
 *   4. Scans, pairs, and connects to mobile phones (Android / iOS).
 *   5. Detects incoming calls via HFP and pushes real-time Server-Sent Events (SSE).
 *   6. Web dashboard provides:
 *      - Audio ringtone synthesis (zero external files, 100% offline).
 *      - Browser Desktop Notifications API integration.
 *      - Live incoming call visual banner & vibration animation.
 *      - Bluetooth device discovery & 1-click connect/disconnect.
 *      - Live WiFi configuration & status monitoring.
 *      - Local phonebook/contacts mapping saved in browser storage.
 *      - Recent call history log.
 *
 * Board Settings in Arduino IDE:
 *   - Board: "ESP32 Dev Module"
 *   - Partition Scheme: "Default 4MB with spiffs" or "Minimal SPIFFS (1.9MB APP)"
 *   - Flash Frequency: 80MHz
 *   - Upload Speed: 921600 or 115200
 */

#include <WiFi.h>
#include <Preferences.h>
#include "esp_wifi.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"

// ── Configuration Constants ──────────────────────────────────────────────────
#define AP_SSID_DEFAULT     "ESP32_CallNotifier"
#define AP_PASS_DEFAULT     "12345678"
#define MAX_SSE_CLIENTS     8
#define MAX_DISCOVERED      20
#define HTTP_PORT           80

// ── Data Structures ──────────────────────────────────────────────────────────
struct DiscoveredDevice {
    uint8_t bda[6];
    char mac[18];
    char name[64];
    int rssi;
};

// ── Global State Variables ───────────────────────────────────────────────────
Preferences prefs;

// WiFi state
String sta_ssid = "";
String sta_password = "";
bool sta_connected = false;
int ap_client_count = 0;

// Bluetooth state
bool bt_initialized = false;
bool bt_scanning = false;
esp_hf_connection_state_t hf_conn_state = ESP_HF_CONNECTION_STATE_DISCONNECTED;
uint8_t connected_bda[6] = {0};
String connected_mac_str = "";
String connected_name_str = "None";

// Saved BT device (for auto-reconnect)
uint8_t saved_bt_mac[6] = {0};
String saved_bt_mac_str = "";
String saved_bt_name_str = "";
bool saved_bt_mac_valid = false;
bool user_disconnected = false;
unsigned long last_bt_reconnect_attempt = 0;

// Discovered BT devices
DiscoveredDevice discoveredDevices[MAX_DISCOVERED];
int discoveredCount = 0;

// Call State
bool call_ringing = false;
bool call_active = false;
String current_caller_number = "";
String last_caller_number = "";
unsigned long call_start_millis = 0;
unsigned long last_call_timestamp = 0;

// SSE & Web Server
WiFiServer server(HTTP_PORT);
WiFiClient sseClients[MAX_SSE_CLIENTS];
SemaphoreHandle_t sseMutex = NULL;
unsigned long lastSsePing = 0;

// ── MAC Address Helpers ──────────────────────────────────────────────────────
bool parseMac(const String& s, uint8_t out[6]) {
    if (s.length() != 17) return false;
    unsigned int m[6];
    if (sscanf(s.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
               &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
        for (int i = 0; i < 6; i++) out[i] = (uint8_t)m[i];
        return true;
    }
    return false;
}

String formatMac(const uint8_t mac[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

const char* getHfStateStr(esp_hf_connection_state_t state) {
    switch (state) {
        case ESP_HF_CONNECTION_STATE_DISCONNECTED: return "Disconnected";
        case ESP_HF_CONNECTION_STATE_CONNECTING:   return "Connecting";
        case ESP_HF_CONNECTION_STATE_CONNECTED:    return "Connected (RFCOMM)";
        case ESP_HF_CONNECTION_STATE_SLC_CONNECTED:return "Connected (Hands-Free)";
        case ESP_HF_CONNECTION_STATE_DISCONNECTING:return "Disconnecting";
        default: return "Unknown";
    }
}

// ── NVS Configuration Helpers ────────────────────────────────────────────────
void loadConfig() {
    prefs.begin("callnotif", true);
    sta_ssid = prefs.getString("sta_ssid", "");
    sta_password = prefs.getString("sta_pwd", "");
    saved_bt_mac_str = prefs.getString("bt_mac", "");
    saved_bt_name_str = prefs.getString("bt_name", "");
    prefs.end();

    if (saved_bt_mac_str.length() == 17 && parseMac(saved_bt_mac_str, saved_bt_mac)) {
        saved_bt_mac_valid = true;
        Serial.printf("[NVS] Loaded saved BT device: %s (%s)\n", saved_bt_name_str.c_str(), saved_bt_mac_str.c_str());
    } else {
        saved_bt_mac_valid = false;
    }

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

void saveBtConfig(const String& mac, const String& name) {
    prefs.begin("callnotif", false);
    prefs.putString("bt_mac", mac);
    prefs.putString("bt_name", name);
    prefs.end();
    saved_bt_mac_str = mac;
    saved_bt_name_str = name;
    saved_bt_mac_valid = parseMac(mac, saved_bt_mac);
    Serial.printf("[NVS] Saved BT device: %s (%s)\n", name.c_str(), mac.c_str());
}

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

// ── WiFi Event Handler ───────────────────────────────────────────────────────
static void onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            sta_connected = true;
            Serial.printf("[WiFi] STA connected! IP: %s | GW: %s | DNS: %s\n",
                          WiFi.localIP().toString().c_str(),
                          WiFi.gatewayIP().toString().c_str(),
                          WiFi.dnsIP().toString().c_str());
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            sta_connected = false;
            Serial.println("[WiFi] STA disconnected");
            if (sta_ssid.length() > 0) {
                // Auto-reconnect to target AP
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

// ── Bluetooth GAP Callback (Discovery & Security) ────────────────────────────
static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            char bdaStr[18];
            snprintf(bdaStr, sizeof(bdaStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     param->disc_res.bda[0], param->disc_res.bda[1], param->disc_res.bda[2],
                     param->disc_res.bda[3], param->disc_res.bda[4], param->disc_res.bda[5]);

            char name[64] = "";
            int rssi = -100;
            bool nameFound = false;

            for (int i = 0; i < param->disc_res.num_prop; i++) {
                esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
                if (p->type == ESP_BT_GAP_DEV_PROP_BDNAME && p->len > 0) {
                    int l = p->len < (int)sizeof(name) - 1 ? p->len : (int)sizeof(name) - 1;
                    memcpy(name, p->val, l);
                    name[l] = '\0';
                    nameFound = true;
                } else if (p->type == ESP_BT_GAP_DEV_PROP_RSSI && p->len == sizeof(int8_t)) {
                    rssi = *(int8_t *)(p->val);
                } else if (p->type == ESP_BT_GAP_DEV_PROP_EIR && !nameFound) {
                    uint8_t eirLen = 0;
                    uint8_t *eirName = esp_bt_gap_resolve_eir_data((uint8_t *)p->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &eirLen);
                    if (!eirName) {
                        eirName = esp_bt_gap_resolve_eir_data((uint8_t *)p->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &eirLen);
                    }
                    if (eirName && eirLen > 0) {
                        int l = eirLen < (uint8_t)sizeof(name) - 1 ? eirLen : (uint8_t)sizeof(name) - 1;
                        memcpy(name, eirName, l);
                        name[l] = '\0';
                        nameFound = true;
                    }
                }
            }

            // Update existing or add new device
            bool found = false;
            for (int i = 0; i < discoveredCount; i++) {
                if (memcmp(discoveredDevices[i].bda, param->disc_res.bda, 6) == 0) {
                    discoveredDevices[i].rssi = rssi;
                    if (nameFound && strlen(name) > 0) {
                        strncpy(discoveredDevices[i].name, name, sizeof(discoveredDevices[i].name) - 1);
                        discoveredDevices[i].name[sizeof(discoveredDevices[i].name) - 1] = '\0';
                    }
                    found = true;
                    break;
                }
            }
            if (!found && discoveredCount < MAX_DISCOVERED) {
                memcpy(discoveredDevices[discoveredCount].bda, param->disc_res.bda, 6);
                strncpy(discoveredDevices[discoveredCount].mac, bdaStr, sizeof(discoveredDevices[discoveredCount].mac) - 1);
                discoveredDevices[discoveredCount].mac[sizeof(discoveredDevices[discoveredCount].mac) - 1] = '\0';
                if (nameFound && strlen(name) > 0) {
                    strncpy(discoveredDevices[discoveredCount].name, name, sizeof(discoveredDevices[discoveredCount].name) - 1);
                } else {
                    strncpy(discoveredDevices[discoveredCount].name, "Bluetooth Device", sizeof(discoveredDevices[discoveredCount].name) - 1);
                }
                discoveredDevices[discoveredCount].name[sizeof(discoveredDevices[discoveredCount].name) - 1] = '\0';
                discoveredDevices[discoveredCount].rssi = rssi;
                discoveredCount++;
                Serial.printf("[BT GAP] Discovered #%d: %s (%s, RSSI: %d)\n",
                              discoveredCount, discoveredDevices[discoveredCount-1].name, bdaStr, rssi);
            }
            break;
        }

        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                Serial.println("[BT GAP] Inquiry scan finished");
                bt_scanning = false;
            } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
                Serial.println("[BT GAP] Inquiry scan started...");
                bt_scanning = true;
            }
            break;

        case ESP_BT_GAP_CFM_REQ_EVT:
            Serial.printf("[BT GAP] SSP Auto-confirm pairing with %02X:%02X:%02X:%02X:%02X:%02X\n",
                          param->cfm_req.bda[0], param->cfm_req.bda[1], param->cfm_req.bda[2],
                          param->cfm_req.bda[3], param->cfm_req.bda[4], param->cfm_req.bda[5]);
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;

        case ESP_BT_GAP_PIN_REQ_EVT: {
            Serial.println("[BT GAP] Legacy PIN code request, replying '0000'");
            esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
            break;
        }

        case ESP_BT_GAP_AUTH_CMPL_EVT:
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                Serial.printf("[BT GAP] Pairing authentication success: %s\n", param->auth_cmpl.device_name);
                if (strlen((char*)param->auth_cmpl.device_name) > 0) {
                    connected_name_str = String((char*)param->auth_cmpl.device_name);
                }
            } else {
                Serial.printf("[BT GAP] Pairing failed with status %d\n", param->auth_cmpl.stat);
            }
            break;

        default:
            break;
    }
}

// ── Bluetooth HFP Client Callback ────────────────────────────────────────────
static void bt_hf_client_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param) {
    switch (event) {
        case ESP_HF_CLIENT_CONNECTION_STATE_EVT: {
            hf_conn_state = param->conn_stat.state;
            Serial.printf("[HFP] Connection State: %s (%d)\n", getHfStateStr(hf_conn_state), hf_conn_state);

            if (hf_conn_state == ESP_HF_CONNECTION_STATE_SLC_CONNECTED) {
                memcpy(connected_bda, param->conn_stat.remote_bda, 6);
                connected_mac_str = formatMac(connected_bda);

                // Check if device name exists in discovered list
                for (int i = 0; i < discoveredCount; i++) {
                    if (memcmp(discoveredDevices[i].bda, connected_bda, 6) == 0 && strlen(discoveredDevices[i].name) > 0) {
                        connected_name_str = String(discoveredDevices[i].name);
                        break;
                    }
                }
                if (connected_name_str == "None" && saved_bt_name_str.length() > 0 && saved_bt_mac_str == connected_mac_str) {
                    connected_name_str = saved_bt_name_str;
                }

                saveBtConfig(connected_mac_str, connected_name_str);
                user_disconnected = false;

                String json = "{\"state\":\"connected\",\"mac\":\"" + connected_mac_str + "\",\"name\":\"" + connected_name_str + "\"}";
                broadcastSSE("bt_status", json);
            } else if (hf_conn_state == ESP_HF_CONNECTION_STATE_DISCONNECTED) {
                call_ringing = false;
                call_active = false;
                current_caller_number = "";
                connected_mac_str = "";
                connected_name_str = "None";

                String json = "{\"state\":\"disconnected\",\"mac\":\"\",\"name\":\"\"}";
                broadcastSSE("bt_status", json);
            } else if (hf_conn_state == ESP_HF_CONNECTION_STATE_CONNECTING) {
                String json = "{\"state\":\"connecting\",\"mac\":\"\",\"name\":\"\"}";
                broadcastSSE("bt_status", json);
            }
            break;
        }

        case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT: {
            Serial.printf("[HFP] Call Setup Indicator: %d\n", param->call_setup.status);
            if (param->call_setup.status == ESP_HF_CALL_SETUP_STATUS_INCOMING) {
                call_ringing = true;
                call_start_millis = millis();
                Serial.println("\n🔔 *** INCOMING PHONE CALL DETECTED *** 🔔");

                String json = "{\"number\":\"" + current_caller_number + "\",\"name\":\"\"}";
                broadcastSSE("incoming_call", json);
            } else if (param->call_setup.status == ESP_HF_CALL_SETUP_STATUS_IDLE) {
                if (call_ringing && !call_active) {
                    // Missed or rejected call before answering
                    call_ringing = false;
                    Serial.println("[HFP] Incoming call dismissed / missed");
                    broadcastSSE("call_ended", "{}");
                    current_caller_number = "";
                }
            }
            break;
        }

        case ESP_HF_CLIENT_CLIP_EVT: {
            if (param->clip.number != NULL && strlen(param->clip.number) > 0) {
                current_caller_number = String(param->clip.number);
                last_caller_number = current_caller_number;
                last_call_timestamp = millis();
                Serial.printf("[HFP] Caller ID (CLIP): %s\n", current_caller_number.c_str());

                String json = "{\"number\":\"" + current_caller_number + "\",\"name\":\"\"}";
                broadcastSSE("incoming_call", json);
            }
            break;
        }

        case ESP_HF_CLIENT_CIND_CALL_EVT: {
            Serial.printf("[HFP] Call Indicator status: %d\n", param->call.status);
            if (param->call.status == 1) {
                // Call answered / in progress
                call_ringing = false;
                call_active = true;
                Serial.println("[HFP] Call Active (Answered)");
                String json = "{\"number\":\"" + current_caller_number + "\"}";
                broadcastSSE("call_answered", json);
            } else if (param->call.status == 0) {
                // Call ended
                if (call_active || call_ringing) {
                    call_active = false;
                    call_ringing = false;
                    Serial.println("[HFP] Call Ended / Hung up");
                    broadcastSSE("call_ended", "{}");
                    current_caller_number = "";
                }
            }
            break;
        }

        case ESP_HF_CLIENT_PROF_STATE_EVT:
            Serial.printf("[HFP] HFP Client Profile initialized (status: %d)\n", param->prof_stat.state);
            break;

        default:
            break;
    }
}

// ── Bluetooth Stack Initialization ───────────────────────────────────────────
bool initBluetooth() {
    Serial.println("[BT] Initializing Classic Bluetooth subsystem...");

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        Serial.printf("[BT] ERROR: esp_bt_controller_init failed: 0x%x\n", ret);
        return false;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        Serial.printf("[BT] ERROR: esp_bt_controller_enable failed: 0x%x\n", ret);
        return false;
    }

    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        Serial.printf("[BT] ERROR: esp_bluedroid_init failed: 0x%x\n", ret);
        return false;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        Serial.printf("[BT] ERROR: esp_bluedroid_enable failed: 0x%x\n", ret);
        return false;
    }

    // Set local Bluetooth friendly name
    esp_bt_dev_set_device_name("ESP32_CallNotifier");

    // Secure Simple Pairing (SSP) configuration
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE; // "Just Works" pairing without PIN prompt
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    // Register GAP callback
    esp_bt_gap_register_callback(bt_gap_cb);

    // Register HFP client callback and initialize
    ret = esp_hf_client_register_callback(bt_hf_client_cb);
    if (ret != ESP_OK) {
        Serial.printf("[BT] ERROR: esp_hf_client_register_callback failed: 0x%x\n", ret);
        return false;
    }

    ret = esp_hf_client_init();
    if (ret != ESP_OK) {
        Serial.printf("[BT] ERROR: esp_hf_client_init failed: 0x%x\n", ret);
        return false;
    }

    // Enable discoverable and connectable scan mode
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    bt_initialized = true;
    Serial.println("[BT] Classic Bluetooth HFP Client initialized successfully!");
    return true;
}

// ── Embedded Web Application (HTML / CSS / JavaScript) ───────────────────────
static const char HTML_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 Call Notifier</title>
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
.dev-list{display:flex;flex-direction:column;gap:8px;margin-top:12px;max-height:220px;overflow-y:auto;}
.dev-item{display:flex;align-items:center;justify-content:space-between;background:#151518;border:1px solid var(--border);padding:10px 14px;border-radius:8px;}
.dev-info{display:flex;flex-direction:column;}
.dev-name{font-weight:600;font-size:14px;color:#fff;}
.dev-mac{font-size:11px;color:var(--muted);font-family:monospace;}
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
    <div class="title">&#x1F4E1; ESP32 Call Notifier</div>
    <div class="badges">
      <div class="badge" title="SSE Stream Status">
        <span id="sse-dot" class="dot warn"></span>
        <span id="sse-txt">SSE: Connecting</span>
      </div>
      <div class="badge" title="Bluetooth Status">
        <span id="bt-dot" class="dot fail"></span>
        <span id="bt-txt">BT: Disconnected</span>
      </div>
    </div>
  </header>

  <!-- Notification Center Card -->
  <div class="card">
    <div class="card-head">
      <h2>&#x1F514; Desktop Notifications &amp; Audio</h2>
      <span id="notif-perm-badge" class="badge">Checking...</span>
    </div>
    <p class="hint">Incoming calls trigger high-priority alerts with caller ID. Web Audio plays instant ring chimes without external assets.</p>
    <div class="btn-row">
      <button id="btn-perm" onclick="requestNotificationPerm()">&#x1F514; Enable Desktop Notifications</button>
      <button class="sec" onclick="testNotification()">&#x25B6; Test Alert &amp; Sound</button>
    </div>
    <div id="chrome-flag-tip" class="hint" style="display:none;margin-top:10px;padding:8px 12px;background:#27272a;border-radius:6px;color:#fbbf24;">
      <b>Chrome/Edge Tip:</b> If notifications are blocked over HTTP, go to <code>chrome://flags/#unsafely-treat-insecure-origin-as-secure</code> and add your ESP32's IP (<span id="tip-ip">http://192.168.4.1</span>).
    </div>
  </div>

  <!-- Bluetooth HFP Manager Card -->
  <div class="card">
    <div class="card-head">
      <h2>&#x1F4F1; Bluetooth Phone (HFP)</h2>
      <button id="btn-scan" class="sec" onclick="startBtScan()">&#x1F50D; Scan Devices</button>
    </div>
    <div class="grid2">
      <div class="stat-box">
        <b>Link Status</b>
        <span id="bt-state-display">Disconnected</span>
      </div>
      <div class="stat-box">
        <b>Connected Phone</b>
        <span id="bt-dev-display">None</span>
      </div>
    </div>
    <div id="bt-disconnect-row" style="display:none;margin-top:10px;">
      <button class="danger" style="width:100%;" onclick="disconnectBt()">Disconnect Phone</button>
    </div>
    <label style="margin-top:14px;">Discovered Bluetooth Devices</label>
    <div id="dev-list" class="dev-list">
      <div class="hint" style="padding:10px;text-align:center;">Click "Scan Devices" to find nearby phones, or select "ESP32_CallNotifier" from your phone's Bluetooth settings.</div>
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
      <h2>&#x1F4CB; Recent Calls</h2>
      <button class="sec" style="font-size:11px;padding:4px 10px;" onclick="clearHistory()">Clear</button>
    </div>
    <table class="history-table">
      <thead>
        <tr><th>Time</th><th>Caller</th><th>Status</th></tr>
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
function resolveCaller(rawNum) {
  if (!rawNum) return { name: 'Unknown Caller', number: 'Private / Withheld' };
  const clean = rawNum.replace(/\s+/g,'');
  const c = getContacts();
  for (let key in c) {
    if (clean.endsWith(key) || key.endsWith(clean)) return { name: c[key], number: rawNum };
  }
  return { name: rawNum, number: rawNum };
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
function addHistory(num, status) {
  const h = getHistory();
  const now = new Date();
  const timeStr = now.toLocaleTimeString([], { hour:'2-digit', minute:'2-digit', second:'2-digit' });
  const info = resolveCaller(num);
  h.unshift({ time: timeStr, caller: info.name !== info.number ? `${info.name} (${info.number})` : info.number, status: status });
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
    const isMissed = item.status.toLowerCase().includes('missed');
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
  getAudioContext(); // Unlock audio on user click
  if (!('Notification' in window)) {
    alert('This browser does not support desktop notifications.');
    return;
  }
  Notification.requestPermission().then(p => {
    updateNotifPermBadge();
    if (p === 'granted') {
      new Notification('ESP32 Call Notifier', {
        body: 'Desktop notifications are active!',
        icon: 'data:image/svg+xml,<svg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220%22><text y=%2226%22 font-size=%2224%22>📞</text></svg>'
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
        icon: 'data:image/svg+xml,<svg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 32 32%22><text y=%2226%22 font-size=%2226%22>📞</text></svg>'
      });
    } catch(e) { console.error('Notification error:', e); }
  }
}

function testNotification() {
  getAudioContext();
  startRingtone();
  setTimeout(stopRingtone, 2500);
  showDesktopNotification('📞 Test Call Alert', 'Desktop notifications and audio synthesizer are working!');
  handleIncomingCall('+1 (555) 019-2834');
}

// ── Incoming Call Handlers ───────────────────────────────────────────────────
let currentCallNumber = '';

function handleIncomingCall(num) {
  currentCallNumber = num || currentCallNumber;
  const info = resolveCaller(currentCallNumber);
  document.getElementById('call-number').innerText = info.number;
  document.getElementById('call-contact').innerText = info.name !== info.number ? info.name : '';
  document.getElementById('call-status-lbl').innerText = 'INCOMING PHONE CALL';
  document.getElementById('call-modal').classList.add('active');
  document.getElementById('btn-silence').innerText = '🔇 Silence Ring';
  startRingtone();
  showDesktopNotification('📞 Incoming Call', `${info.name !== info.number ? info.name + ' (' + info.number + ')' : info.number} is calling...`);
}

function handleCallAnswered() {
  stopRingtone();
  document.getElementById('call-status-lbl').innerText = 'CALL IN PROGRESS';
  document.getElementById('call-status-lbl').style.color = 'var(--success)';
  addHistory(currentCallNumber, 'Answered');
}

function handleCallEnded() {
  stopRingtone();
  const modal = document.getElementById('call-modal');
  if (modal.classList.contains('active')) {
    addHistory(currentCallNumber, 'Missed / Ended');
  }
  document.getElementById('call-status-lbl').innerText = 'CALL ENDED';
  setTimeout(() => { dismissCallModal(); }, 2000);
}

function dismissCallModal() {
  stopRingtone();
  document.getElementById('call-modal').classList.remove('active');
  document.getElementById('call-status-lbl').style.color = 'var(--danger)';
  currentCallNumber = '';
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
      handleIncomingCall(data.number);
    } catch(err) { console.error(err); }
  });

  eventSource.addEventListener('call_answered', (e) => {
    handleCallAnswered();
  });

  eventSource.addEventListener('call_ended', () => {
    handleCallEnded();
  });

  eventSource.addEventListener('bt_status', (e) => {
    try {
      const data = JSON.parse(e.data || '{}');
      updateBtUI(data);
    } catch(err) { console.error(err); }
  });
}

// ── Bluetooth & WiFi AJAX Operations ─────────────────────────────────────────
function updateBtUI(data) {
  const isConn = (data.state === 'connected' || data.state_code === 3);
  document.getElementById('bt-dot').className = isConn ? 'dot ok' : (data.state === 'connecting' ? 'dot warn' : 'dot fail');
  document.getElementById('bt-txt').innerText = 'BT: ' + (isConn ? 'Connected' : (data.state || 'Disconnected'));
  document.getElementById('bt-state-display').innerText = data.state || (isConn ? 'Connected' : 'Disconnected');
  document.getElementById('bt-dev-display').innerText = data.name && data.name !== 'None' ? `${data.name} (${data.mac})` : 'None';
  document.getElementById('bt-disconnect-row').style.display = isConn ? 'block' : 'none';
}

function startBtScan() {
  const btn = document.getElementById('btn-scan');
  btn.disabled = true;
  btn.innerText = 'Scanning (10s)...';
  fetch('/bt/scan', { method: 'POST' })
    .then(r => r.json())
    .then(() => {
      let attempts = 0;
      const pollTimer = setInterval(() => {
        attempts++;
        fetch('/bt/devices')
          .then(r => r.json())
          .then(devs => {
            renderDeviceList(devs);
          });
        if (attempts >= 10) {
          clearInterval(pollTimer);
          btn.disabled = false;
          btn.innerText = '🔍 Scan Devices';
        }
      }, 1500);
    })
    .catch(() => { btn.disabled = false; btn.innerText = '🔍 Scan Devices'; });
}

function renderDeviceList(devs) {
  const container = document.getElementById('dev-list');
  if (!devs || devs.length === 0) {
    container.innerHTML = '<div class="hint" style="padding:10px;text-align:center;">No devices found yet. Ensure phone Bluetooth is discoverable.</div>';
    return;
  }
  let h = '';
  for (let d of devs) {
    h += `<div class="dev-item">
      <div class="dev-info">
        <span class="dev-name">${escapeHtml(d.name || 'Bluetooth Device')}</span>
        <span class="dev-mac">${escapeHtml(d.mac)} &nbsp;&bull;&nbsp; ${d.rssi} dBm</span>
      </div>
      <button class="sec" style="padding:6px 12px;font-size:12px;" onclick="connectBt('${escapeHtml(d.mac)}', '${escapeHtml(d.name)}')">Connect</button>
    </div>`;
  }
  container.innerHTML = h;
}

function connectBt(mac, name) {
  document.getElementById('bt-state-display').innerText = 'Connecting to ' + mac + '...';
  fetch('/bt/connect?mac=' + encodeURIComponent(mac), { method: 'POST' })
    .then(r => r.json())
    .then(data => {
      console.log('Connect response:', data);
    });
}

function disconnectBt() {
  fetch('/bt/disconnect', { method: 'POST' })
    .then(r => r.json())
    .then(() => {
      document.getElementById('bt-state-display').innerText = 'Disconnected';
      document.getElementById('bt-dev-display').innerText = 'None';
      document.getElementById('bt-disconnect-row').style.display = 'none';
    });
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
      if (d.bt) {
        updateBtUI(d.bt);
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
        if (h.length() == 0) break; // empty line marks end of headers
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

                // Send current initial BT status
                String statusData = "{\"state\":\"" + String(getHfStateStr(hf_conn_state)) + "\",\"mac\":\"" + 
                                    connected_mac_str + "\",\"name\":\"" + connected_name_str + "\"}";
                client.printf("event: bt_status\ndata: %s\n\n", statusData.c_str());
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
        return; // Connection intentionally left open for SSE
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

        json += "\"bt\":{";
        json += "\"initialized\":" + String(bt_initialized ? "true" : "false") + ",";
        json += "\"state\":\"" + String(getHfStateStr(hf_conn_state)) + "\",";
        json += "\"state_code\":" + String(hf_conn_state) + ",";
        json += "\"scanning\":" + String(bt_scanning ? "true" : "false") + ",";
        json += "\"connected_name\":\"" + connected_name_str + "\",";
        json += "\"connected_mac\":\"" + connected_mac_str + "\",";
        json += "\"saved_mac\":\"" + saved_bt_mac_str + "\"";
        json += "},";

        json += "\"call\":{";
        json += "\"ringing\":" + String(call_ringing ? "true" : "false") + ",";
        json += "\"active\":" + String(call_active ? "true" : "false") + ",";
        json += "\"number\":\"" + current_caller_number + "\",";
        json += "\"last_number\":\"" + last_caller_number + "\",";
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

    // Route 5: Start Bluetooth Inquiry Scan (/bt/scan)
    if (method == "POST" && path == "/bt/scan") {
        discoveredCount = 0;
        esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
        client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n");
        client.print("{\"success\":true,\"message\":\"Inquiry discovery started\"}");
        client.stop();
        return;
    }

    // Route 6: List Discovered Devices (/bt/devices)
    if (method == "GET" && path == "/bt/devices") {
        String json = "[";
        for (int i = 0; i < discoveredCount; i++) {
            if (i > 0) json += ",";
            json += "{\"mac\":\"" + String(discoveredDevices[i].mac) + "\",";
            json += "\"name\":\"" + String(discoveredDevices[i].name) + "\",";
            json += "\"rssi\":" + String(discoveredDevices[i].rssi) + "}";
        }
        json += "]";

        client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n");
        client.print(json);
        client.stop();
        return;
    }

    // Route 7: Connect to Bluetooth Device (/bt/connect)
    if (method == "POST" && path == "/bt/connect") {
        String data = query.length() > 0 ? query : body;
        String mac = getParam(data, "mac");
        uint8_t target_bda[6];
        if (parseMac(mac, target_bda)) {
            // Find name if discovered
            for (int i = 0; i < discoveredCount; i++) {
                if (memcmp(discoveredDevices[i].bda, target_bda, 6) == 0 && strlen(discoveredDevices[i].name) > 0) {
                    connected_name_str = String(discoveredDevices[i].name);
                    break;
                }
            }
            saveBtConfig(mac, connected_name_str);
            user_disconnected = false;
            esp_hf_client_connect(target_bda);

            client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n");
            client.print("{\"success\":true,\"message\":\"Connecting to device...\"}");
        } else {
            client.print("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n{\"error\":\"Invalid MAC address\"}");
        }
        client.stop();
        return;
    }

    // Route 8: Disconnect Bluetooth Device (/bt/disconnect)
    if (method == "POST" && path == "/bt/disconnect") {
        user_disconnected = true;
        esp_hf_client_disconnect(connected_bda);
        client.print("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n");
        client.print("{\"success\":true,\"message\":\"Disconnecting...\"}");
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
    Serial.println("       ESP32 HFP Call Notifier v1.0           ");
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

    // Disable WiFi power save for reliable WiFi + Classic BT coexistence
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Auto-connect to saved WiFi station network
    if (sta_ssid.length() > 0) {
        Serial.printf("[WiFi] Auto-connecting to saved AP: '%s'...\n", sta_ssid.c_str());
        WiFi.begin(sta_ssid.c_str(), sta_password.c_str());
    }

    // 4. Initialize Classic Bluetooth HFP subsystem
    if (!initBluetooth()) {
        Serial.println("[BT] WARNING: Classic Bluetooth initialization reported issues.");
    }

    // 5. Start Web Server
    server.begin();
    Serial.println("[HTTP] Web server listening on port 80");
    Serial.println("[READY] Open http://192.168.4.1 in your browser to configure.\n");
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

    // 3. Auto-reconnect to saved Bluetooth phone if disconnected
    if (bt_initialized && saved_bt_mac_valid && !user_disconnected && !bt_scanning) {
        if (hf_conn_state == ESP_HF_CONNECTION_STATE_DISCONNECTED) {
            if (now - last_bt_reconnect_attempt >= 25000) {
                last_bt_reconnect_attempt = now;
                Serial.printf("[BT] Auto-reconnecting to saved phone: %s (%s)...\n",
                              saved_bt_name_str.c_str(), saved_bt_mac_str.c_str());
                esp_hf_client_connect(saved_bt_mac);
            }
        }
    }

    delay(2); // Yield to FreeRTOS watchdog & background tasks
}

