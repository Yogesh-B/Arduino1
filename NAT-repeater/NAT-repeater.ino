/*
 * ESP32 NAT Router / Repeater  (Arduino IDE — single file)
 * ─────────────────────────────────────────────────────────
 * Tested on: ESP32-S3 / Arduino-ESP32 core 3.x (v3.3.12)
 *
 * How it works:
 *   STA connects to your main AP (uplink) and gets an IP via DHCP.
 *   SoftAP (192.168.4.0/24) runs a DHCP server for downstream clients.
 *   lwIP NAPT (Network Address Port Translation) rewrites packets so
 *   downstream clients can reach the internet through the STA uplink.
 *   DNS from the uplink is forwarded to downstream clients via DHCPS option.
 *
 * First boot:
 *   AP "ESP32_NAT_Router" (password: 12345678) appears.
 *   Connect to it → http://192.168.4.1 → configure uplink + repeater.
 *   Settings (including optional MAC spoofing) are persisted in NVS.
 *
 * What was wrong with the previous version:
 *   1. loadConfig() read NVS before prefs.begin() — garbled values.
 *   2. Used low-level ip_napt_enable() with manual PP_HTONL byte-swap
 *      which was fragile across core versions.
 *   3. DNS was set via esp_netif_set_dns_info() on the AP netif, but
 *      that only changes the *interface's own* DNS — it does NOT inject
 *      the DNS server into the DHCP offers sent to AP clients.  Clients
 *      got 0.0.0.0 as DNS → no name resolution → "no internet".
 *   4. No auto-reconnect if the uplink dropped.
 *
 * Fixes applied:
 *   • Use esp_netif_napt_enable() — the proper high-level API.
 *   • Forward DNS to clients via dhcps_offer_t / DHCPS option 6.
 *   • Event-driven WiFi with automatic reconnect.
 *   • Correct loadConfig() ordering.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <lwip/lwip_napt.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "dhcpserver/dhcpserver.h"

// ── RGB LED (ESP32-S3-DevKitC onboard) ───────────────────────────────────────
#define RGB_LED_PIN 48

// ── Forward declarations ─────────────────────────────────────────────────────
static void enableNAPT();
static void forwardDNStoClients();

// ── Config stored in NVS ─────────────────────────────────────────────────────
Preferences prefs;

String sta_ssid     = "";
String sta_password = "";
String ap_ssid      = "ESP32_NAT_Router";
String ap_password  = "12345678";
String sta_mac_str  = "";   // empty = use chip default
String ap_mac_str   = "";   // empty = use chip default

// ── Helpers: MAC parse / format ──────────────────────────────────────────────

bool parseMac(const String& s, uint8_t out[6]) {
    if (s.length() != 17) return false;
    return sscanf(s.c_str(),
        "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
        &out[0], &out[1], &out[2], &out[3], &out[4], &out[5]) == 6;
}

String formatMac(const uint8_t mac[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

// ── NVS load / save ──────────────────────────────────────────────────────────

void loadConfig() {
    prefs.begin("rtr", true);               // open FIRST, then read
    sta_ssid     = prefs.getString("sta_ssid", "");
    sta_password = prefs.getString("sta_pwd",  "");
    ap_ssid      = prefs.getString("ap_ssid",  "ESP32_NAT_Router");
    ap_password  = prefs.getString("ap_pwd",   "12345678");
    sta_mac_str  = prefs.getString("sta_mac",  "");
    ap_mac_str   = prefs.getString("ap_mac",   "");
    prefs.end();
}

void saveConfig(const String& ss, const String& sp,
                const String& as_, const String& ap,
                const String& smac, const String& amac) {
    prefs.begin("rtr", false);
    prefs.putString("sta_ssid", ss);
    prefs.putString("sta_pwd",  sp);
    prefs.putString("ap_ssid",  as_);
    prefs.putString("ap_pwd",   ap);
    prefs.putString("sta_mac",  smac);
    prefs.putString("ap_mac",   amac);
    prefs.end();
}

// ── NAPT + DNS forwarding ────────────────────────────────────────────────────

static bool napt_active = false;

/**
 * Enable NAPT on the SoftAP interface using the high-level esp_netif API.
 * This is the CORRECT way — works regardless of byte-order quirks.
 */
static void enableNAPT() {
    if (napt_active) return;

    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap_netif) {
        Serial.println("[NAT] ERROR: Could not find AP netif handle");
        return;
    }

    esp_err_t err = esp_netif_napt_enable(ap_netif);
    if (err == ESP_OK) {
        napt_active = true;
        Serial.println("[NAT] NAPT enabled  →  NAT routing is ACTIVE");
    } else {
        Serial.printf("[NAT] esp_netif_napt_enable failed: 0x%x (%s)\n",
                      err, esp_err_to_name(err));
    }
}

/**
 * Forward the uplink's DNS servers to AP clients via DHCPS.
 *
 * The correct sequence is:
 *   1. Stop DHCPS
 *   2. Set the DNS server info on the AP netif  (this IS what DHCPS reads)
 *   3. Enable the OFFER_DNS flag so option-6 is included in leases
 *   4. Restart DHCPS
 *
 * Calling esp_netif_set_dns_info() while DHCPS is running has no effect on
 * already-issued or future leases — the server must be restarted to pick up
 * the new DNS.
 */
static void forwardDNStoClients() {
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap_netif) return;

    IPAddress dns_primary = WiFi.dnsIP(0);
    IPAddress dns_backup  = WiFi.dnsIP(1);

    if ((uint32_t)dns_primary == 0) {
        Serial.println("[DNS] No uplink DNS available yet — skipping");
        return;
    }

    // 1. Stop DHCPS so we can safely reconfigure it
    esp_netif_dhcps_stop(ap_netif);

    // 2. Set the DNS IPs that DHCPS will advertise in option 6
    {
        esp_netif_dns_info_t dns_info;
        memset(&dns_info, 0, sizeof(dns_info));
        dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        dns_info.ip.u_addr.ip4.addr = static_cast<uint32_t>(dns_primary);
        esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);
        Serial.printf("[DNS] Primary DNS → %s\n", dns_primary.toString().c_str());
    }
    if ((uint32_t)dns_backup != 0) {
        esp_netif_dns_info_t dns_info;
        memset(&dns_info, 0, sizeof(dns_info));
        dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        dns_info.ip.u_addr.ip4.addr = static_cast<uint32_t>(dns_backup);
        esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_BACKUP, &dns_info);
        Serial.printf("[DNS] Backup  DNS → %s\n", dns_backup.toString().c_str());
    }

    // 3. Enable OFFER_DNS flag (option 6 included in DHCP leases)
    uint8_t offer_dns = OFFER_DNS;  // defined in dhcpserver.h as 0x02
    esp_netif_dhcps_option(ap_netif,
        ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
        &offer_dns, sizeof(offer_dns));

    // 4. Restart DHCPS — now it will hand out the correct DNS to clients
    esp_netif_dhcps_start(ap_netif);
    Serial.println("[DNS] DHCPS restarted with new DNS options");
}

// ── WiFi event handler (auto-reconnect + NAPT activation) ────────────────────

static void onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[WiFi] STA connected  →  IP: %s  GW: %s  DNS: %s\n",
                          WiFi.localIP().toString().c_str(),
                          WiFi.gatewayIP().toString().c_str(),
                          WiFi.dnsIP().toString().c_str());
            delay(200);   // let lwIP settle
            enableNAPT();
            forwardDNStoClients();
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("[WiFi] STA disconnected — will auto-reconnect");
            napt_active = false;
            WiFi.reconnect();
            break;

        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            Serial.println("[WiFi] Client connected to AP");
            break;

        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            Serial.println("[WiFi] Client disconnected from AP");
            break;

        default:
            break;
    }
}

// ── Web config portal ────────────────────────────────────────────────────────
WebServer server(80);

static const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP32 NAT Router Setup</title>
<style>
body{font-family:sans-serif;background:#181818;color:#eee;display:flex;justify-content:center;padding:20px;}
.card{background:#242424;padding:25px;border-radius:10px;width:100%;max-width:440px;box-shadow:0 4px 15px rgba(0,0,0,.5);}
h2{margin-top:0;color:#3b82f6;}
h3{color:#94a3b8;border-bottom:1px solid #374151;padding-bottom:6px;}
label{display:block;margin-top:12px;font-size:13px;color:#bbb;}
.hint{font-size:11px;color:#6b7280;margin-top:3px;}
input{width:100%;padding:10px;margin-top:4px;border:1px solid #444;border-radius:6px;background:#333;color:#fff;box-sizing:border-box;font-family:monospace;}
input:focus{outline:none;border-color:#3b82f6;}
.cur{font-size:11px;color:#6b7280;margin-top:4px;font-family:monospace;}
button{margin-top:22px;width:100%;padding:12px;background:#2563eb;color:#fff;border:none;border-radius:6px;font-weight:bold;cursor:pointer;font-size:15px;}
button:hover{background:#1d4ed8;}
.clr{color:#f87171;font-size:11px;cursor:pointer;float:right;margin-top:2px;}
.status{background:#1e293b;border:1px solid #334155;border-radius:8px;padding:12px;margin-bottom:16px;font-size:12px;line-height:1.8;font-family:monospace;}
.ok{color:#4ade80;} .fail{color:#f87171;} .na{color:#94a3b8;}
</style></head><body>
<div class='card'>
<h2>&#x1F4F6; NAT Router Config</h2>

<div class='status'>
<b>Status</b><br>
STA: <span class='%STA_CLASS%'>%STA_STATUS%</span><br>
STA IP: %STA_IP% &nbsp; GW: %STA_GW%<br>
DNS: %STA_DNS%<br>
NAT: <span class='%NAT_CLASS%'>%NAT_STATUS%</span><br>
AP Clients: %AP_CLIENTS%
</div>

<form action='/save' method='POST'>

<h3>Uplink &mdash; connects TO this AP</h3>
<label>Target SSID</label>
<input type='text' name='sta_ssid' value='%STA_SSID%' required>
<label>Target Password</label>
<input type='password' name='sta_pwd' value='%STA_PWD%'>
<label>
  Spoof STA MAC
  <span class='clr' onclick="this.closest('label').nextElementSibling.value=''">clear</span>
</label>
<input type='text' name='sta_mac' value='%STA_MAC%'
  placeholder='AA:BB:CC:DD:EE:FF  (leave empty = chip default)'
  pattern='^([0-9A-Fa-f]{2}[:]){5}[0-9A-Fa-f]{2}$'>
<div class='hint'>This MAC is what the upstream router sees.</div>
<div class='cur'>Current STA MAC: %CUR_STA_MAC%</div>

<h3>Repeater &mdash; clients connect TO this AP</h3>
<label>Repeater SSID</label>
<input type='text' name='ap_ssid' value='%AP_SSID%' required>
<label>Repeater Password (min 8 chars)</label>
<input type='password' name='ap_pwd' value='%AP_PWD%' minlength='8' required>
<label>
  Spoof AP MAC
  <span class='clr' onclick="this.closest('label').nextElementSibling.value=''">clear</span>
</label>
<input type='text' name='ap_mac' value='%AP_MAC%'
  placeholder='AA:BB:CC:DD:EE:FF  (leave empty = chip default)'
  pattern='^([0-9A-Fa-f]{2}[:]){5}[0-9A-Fa-f]{2}$'>
<div class='hint'>This MAC is what scanning devices see as the repeater AP.</div>
<div class='cur'>Current AP MAC: %CUR_AP_MAC%</div>

<button type='submit'>&#x1F4BE; Save &amp; Reboot</button>
</form></div></body></html>
)rawhtml";

void handleRoot() {
    uint8_t sta_mac[6], ap_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, sta_mac);
    esp_wifi_get_mac(WIFI_IF_AP,  ap_mac);

    String page = FPSTR(HTML_PAGE);

    // Config values
    page.replace("%STA_SSID%",    sta_ssid);
    page.replace("%STA_PWD%",     sta_password);
    page.replace("%STA_MAC%",     sta_mac_str);
    page.replace("%AP_SSID%",     ap_ssid);
    page.replace("%AP_PWD%",      ap_password);
    page.replace("%AP_MAC%",      ap_mac_str);
    page.replace("%CUR_STA_MAC%", formatMac(sta_mac));
    page.replace("%CUR_AP_MAC%",  formatMac(ap_mac));

    // Live status
    bool connected = (WiFi.status() == WL_CONNECTED);
    page.replace("%STA_CLASS%",  connected ? "ok" : "fail");
    page.replace("%STA_STATUS%", connected ? "Connected" : "Disconnected");
    page.replace("%STA_IP%",     connected ? WiFi.localIP().toString()  : "—");
    page.replace("%STA_GW%",     connected ? WiFi.gatewayIP().toString(): "—");
    page.replace("%STA_DNS%",    connected ? WiFi.dnsIP().toString()    : "—");
    page.replace("%NAT_CLASS%",  napt_active ? "ok" : "fail");
    page.replace("%NAT_STATUS%", napt_active ? "Active" : "Inactive");
    page.replace("%AP_CLIENTS%", String(WiFi.softAPgetStationNum()));

    server.send(200, "text/html", page);
}

void handleSave() {
    String ss   = server.arg("sta_ssid");
    String sp   = server.arg("sta_pwd");
    String as_  = server.arg("ap_ssid");
    String ap   = server.arg("ap_pwd");
    String smac = server.arg("sta_mac");
    String amac = server.arg("ap_mac");

    // Validate MACs if provided
    uint8_t tmp[6];
    if (smac.length() > 0 && !parseMac(smac, tmp)) {
        server.send(400, "text/html",
            "<html><body><h2>&#x274C; Invalid STA MAC format.</h2>"
            "<p>Use AA:BB:CC:DD:EE:FF</p>"
            "<a href='/'>Go back</a></body></html>");
        return;
    }
    if (amac.length() > 0 && !parseMac(amac, tmp)) {
        server.send(400, "text/html",
            "<html><body><h2>&#x274C; Invalid AP MAC format.</h2>"
            "<p>Use AA:BB:CC:DD:EE:FF</p>"
            "<a href='/'>Go back</a></body></html>");
        return;
    }

    smac.toUpperCase();
    amac.toUpperCase();

    saveConfig(ss, sp, as_, ap, smac, amac);
    server.send(200, "text/html",
        "<html><body><h2>&#x2705; Saved! Rebooting in 2 seconds...</h2></body></html>");
    delay(2000);
    ESP.restart();
}

// ── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(300);

    // Turn off onboard RGB LED
    neopixelWrite(RGB_LED_PIN, 0, 0, 0);

    Serial.println("\n\n=============================");
    Serial.println("  ESP32 NAT Router / Repeater");
    Serial.println("=============================\n");

    loadConfig();

    // Register WiFi event handler BEFORE anything else
    WiFi.onEvent(onWiFiEvent);

    // ── Set WiFi mode ────────────────────────────────────────────────────────
    WiFi.mode(WIFI_AP_STA);

    // ── Apply MAC spoofing BEFORE WiFi.begin / softAP ────────────────────────
    uint8_t mac[6];
    if (sta_mac_str.length() == 17 && parseMac(sta_mac_str, mac)) {
        esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, mac);
        Serial.printf("[MAC] STA spoof %s → %s\n",
                      sta_mac_str.c_str(),
                      err == ESP_OK ? "OK" : "FAILED");
    }
    if (ap_mac_str.length() == 17 && parseMac(ap_mac_str, mac)) {
        esp_err_t err = esp_wifi_set_mac(WIFI_IF_AP, mac);
        Serial.printf("[MAC] AP  spoof %s → %s\n",
                      ap_mac_str.c_str(),
                      err == ESP_OK ? "OK" : "FAILED");
    }

    // ── Start SoftAP on a DIFFERENT subnet than the uplink ───────────────────
    // Default: 192.168.4.0/24 — make sure your main router is NOT 192.168.4.x
    WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),   // AP IP
        IPAddress(192, 168, 4, 1),   // Gateway (itself)
        IPAddress(255, 255, 255, 0)  // Subnet
    );
    WiFi.softAP(ap_ssid.c_str(), ap_password.c_str(), 1, 0, 8);
    // channel=1, hidden=false, max_clients=8

    Serial.printf("[AP]  Started → SSID: \"%s\"  IP: %s\n",
                  ap_ssid.c_str(), WiFi.softAPIP().toString().c_str());

    // Print actual MACs in use
    uint8_t actual_sta[6], actual_ap[6];
    esp_wifi_get_mac(WIFI_IF_STA, actual_sta);
    esp_wifi_get_mac(WIFI_IF_AP,  actual_ap);
    Serial.printf("[MAC] STA: %s\n", formatMac(actual_sta).c_str());
    Serial.printf("[MAC] AP:  %s\n", formatMac(actual_ap).c_str());

    // ── Connect to uplink (non-blocking — event handler does the rest) ───────
    if (sta_ssid.length() > 0) {
        Serial.printf("[STA] Connecting to \"%s\" ...\n", sta_ssid.c_str());
        WiFi.begin(sta_ssid.c_str(), sta_password.c_str());
        WiFi.setAutoReconnect(true);

        // Wait up to 15s for initial connection
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println(" connected!");
            // NAPT + DNS will be enabled by the event handler
        } else {
            Serial.println("\n[STA] Initial connection timed out — will keep retrying");
        }
    } else {
        Serial.println("[STA] No uplink configured");
        Serial.println("      → Connect to AP and visit http://192.168.4.1");
    }

    // ── Start config portal (always available at 192.168.4.1) ────────────────
    server.on("/",     HTTP_GET,  handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.begin();
    Serial.println("[Web] Config portal → http://192.168.4.1\n");
}

// ── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
    server.handleClient();
}
