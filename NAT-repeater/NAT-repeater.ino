/*
 * ESP32 NAT Repeater
 * ------------------
 * On first boot: AP "ESP32_Repeater_Setup" (password: 12345678) comes up.
 * Connect to it and visit http://192.168.4.1 to configure uplink + repeater.
 * Settings (including optional MAC spoofing) are saved to NVS.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <lwip/lwip_napt.h>
#include <lwip/tcpip.h>
#include "esp_wifi.h"
#include "esp_netif.h"

// ── RGB LED ───────────────────────────────────────────────────────────────────
#define RGB_LED_PIN 48

// ── Config stored in NVS ─────────────────────────────────────────────────────
Preferences prefs;

String sta_ssid     = "";
String sta_password = "";
String ap_ssid      = "ESP32_Repeater_Setup";
String ap_password  = "12345678";
String sta_mac_str  = "";   // empty = use chip default
String ap_mac_str   = "";   // empty = use chip default

// Parse "AA:BB:CC:DD:EE:FF" → byte[6], returns true on success
bool parseMac(const String& s, uint8_t out[6]) {
    if (s.length() != 17) return false;
    return sscanf(s.c_str(),
        "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
        &out[0], &out[1], &out[2], &out[3], &out[4], &out[5]) == 6;
}

// Format byte[6] → "AA:BB:CC:DD:EE:FF"
String formatMac(const uint8_t mac[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

void loadConfig() {
    ap_password  = prefs.getString("ap_pwd",      "1231231231");
    prefs.begin("rtr", true);
    sta_ssid     = prefs.getString("sta_ssid", "");
    sta_password = prefs.getString("sta_pwd",  "");
    ap_ssid      = prefs.getString("ap_ssid",  "ESP32_Repeater_Setup");
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

// ── Web config portal ─────────────────────────────────────────────────────────
WebServer server(80);

static const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP32 Repeater Setup</title>
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
</style></head><body>
<div class='card'>
<h2>&#x1F4F6; Repeater Config</h2>
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
<div class='hint'>This MAC is what the upstream router sees when you connect.</div>
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
    // Read current actual MACs from hardware
    uint8_t sta_mac[6], ap_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, sta_mac);
    esp_wifi_get_mac(WIFI_IF_AP,  ap_mac);

    String page = FPSTR(HTML_PAGE);
    page.replace("%STA_SSID%",    sta_ssid);
    page.replace("%STA_PWD%",     sta_password);
    page.replace("%STA_MAC%",     sta_mac_str);
    page.replace("%AP_SSID%",     ap_ssid);
    page.replace("%AP_PWD%",      ap_password);
    page.replace("%AP_MAC%",      ap_mac_str);
    page.replace("%CUR_STA_MAC%", formatMac(sta_mac));
    page.replace("%CUR_AP_MAC%",  formatMac(ap_mac));
    server.send(200, "text/html", page);
}

void handleSave() {
    String ss   = server.arg("sta_ssid");
    String sp   = server.arg("sta_pwd");
    String as_  = server.arg("ap_ssid");
    String ap   = server.arg("ap_pwd");
    String smac = server.arg("sta_mac");
    String amac = server.arg("ap_mac");

    // Validate MACs if provided — reject malformed ones
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

    // Normalise to uppercase
    smac.toUpperCase();
    amac.toUpperCase();

    saveConfig(ss, sp, as_, ap, smac, amac);
    server.send(200, "text/html",
        "<html><body><h2>&#x2705; Saved! Rebooting in 2 seconds...</h2></body></html>");
    delay(2000);
    ESP.restart();
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);

    // Turn off onboard RGB LED immediately
    neopixelWrite(RGB_LED_PIN, 0, 0, 0);

    Serial.println("\n\n=== ESP32 NAT Repeater ===");

    loadConfig();

    // ── Set WiFi mode first (required before MAC changes) ──
    WiFi.mode(WIFI_AP_STA);

    // ── Apply STA MAC spoof BEFORE esp_wifi_start() ──────────────────────────
    // MAC must be set before WiFi starts — this is critical
    uint8_t mac[6];
    if (sta_mac_str.length() == 17 && parseMac(sta_mac_str, mac)) {
        esp_err_t err = esp_wifi_set_mac(WIFI_IF_STA, mac);
        if (err == ESP_OK) {
            Serial.printf("STA MAC spoofed  →  %s\n", sta_mac_str.c_str());
        } else {
            Serial.printf("STA MAC spoof failed (err %d) — using chip default\n", err);
        }
    }

    // ── Apply AP MAC spoof BEFORE esp_wifi_start() ───────────────────────────
    if (ap_mac_str.length() == 17 && parseMac(ap_mac_str, mac)) {
        esp_err_t err = esp_wifi_set_mac(WIFI_IF_AP, mac);
        if (err == ESP_OK) {
            Serial.printf("AP  MAC spoofed  →  %s\n", ap_mac_str.c_str());
        } else {
            Serial.printf("AP  MAC spoof failed (err %d) — using chip default\n", err);
        }
    }

    // ── Start SoftAP ─────────────────────────────────────────────────────────
    WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),
        IPAddress(192, 168, 4, 1),
        IPAddress(255, 255, 255, 0)
    );
    WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());
    Serial.printf("AP started  →  SSID: %s   IP: %s\n",
                  ap_ssid.c_str(), WiFi.softAPIP().toString().c_str());

    // Print actual MACs in use
    uint8_t actual_sta[6], actual_ap[6];
    esp_wifi_get_mac(WIFI_IF_STA, actual_sta);
    esp_wifi_get_mac(WIFI_IF_AP,  actual_ap);
    Serial.printf("STA MAC: %s\n", formatMac(actual_sta).c_str());
    Serial.printf("AP  MAC: %s\n", formatMac(actual_ap).c_str());

    // ── Connect to uplink ─────────────────────────────────────────────────────
    if (sta_ssid.length() > 0) {
        Serial.printf("Connecting to uplink: %s ...\n", sta_ssid.c_str());
        WiFi.begin(sta_ssid.c_str(), sta_password.c_str());

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("\nSTA connected  →  IP: %s  GW: %s  DNS: %s\n",
                          WiFi.localIP().toString().c_str(),
                          WiFi.gatewayIP().toString().c_str(),
                          WiFi.dnsIP().toString().c_str());
            delay(500);  // let lwIP settle

            // ip_napt_enable() needs the AP-subnet IP in network byte order.
            // IPAddress::operator uint32_t() gives host byte order on ESP32,
            // so byte-swap with PP_HTONL before passing it in.
            uint32_t ap_ip_netorder = PP_HTONL((uint32_t)WiFi.softAPIP());
            LOCK_TCPIP_CORE();
            ip_napt_enable(ap_ip_netorder, 1);
            UNLOCK_TCPIP_CORE();
            Serial.println("NAPT enabled  →  NAT routing active");

            // Forward the uplink DNS server to AP clients via DHCP.
            // Without this, clients get no DNS and can't resolve hostnames
            // even though routing/NAT is working fine.
            // WiFiClass has no softAPsetDNS — use esp_netif directly.
            IPAddress dns1 = WiFi.dnsIP(0);
            IPAddress dns2 = WiFi.dnsIP(1);
            if ((uint32_t)dns1 != 0) {
                esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
                if (ap_netif) {
                    esp_netif_dns_info_t dns_info;
                    dns_info.ip.type = ESP_IPADDR_TYPE_V4;
                    // esp_netif stores IPs in network byte order
                    dns_info.ip.u_addr.ip4.addr = PP_HTONL((uint32_t)dns1);
                    esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns_info);
                    if ((uint32_t)dns2 != 0) {
                        dns_info.ip.u_addr.ip4.addr = PP_HTONL((uint32_t)dns2);
                        esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_BACKUP, &dns_info);
                    }
                    Serial.printf("DNS forwarded  →  %s / %s\n",
                                  dns1.toString().c_str(), dns2.toString().c_str());
                }
            }
        } else {
            Serial.println("\nUplink connection failed — running in AP-only mode");
        }
    } else {
        Serial.println("No uplink configured — open http://192.168.4.1 to set up");
    }

    // ── Start config portal ───────────────────────────────────────────────────
    server.on("/",     HTTP_GET,  handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.begin();
    Serial.println("Config portal →  http://192.168.4.1");
}

void loop() {
    server.handleClient();
}
