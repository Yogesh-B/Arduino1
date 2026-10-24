/*
 * ESP32-S3 USB Wi-Fi Dongle  (Arduino IDE — ESP-IDF hybrid)
 * ══════════════════════════════════════════════════════════
 * Turns an ESP32-S3 into a plug-and-play USB Wi-Fi adapter.
 * Plug it into any Windows / Linux / macOS machine and the OS
 * sees a standard network interface — just like a retail Wi-Fi dongle.
 *
 * ┌───────────────┐  USB (CDC-NCM)   ┌──────────────┐
 * │   Host PC     │◄════════════════►│  ESP32-S3    │
 * │  (Win/Linux)  │  Ethernet frames │  WiFi Radio  │~~~)  AP / Internet
 * └───────────────┘                  └──────────────┘
 *
 * ── Features ─────────────────────────────────────────────────
 *   • STA mode  – PC connects to existing WiFi networks via the dongle
 *   • AP  mode  – PC creates a WiFi hotspot through the dongle
 *                 (Windows "Mobile Hotspot" / Linux hostapd)
 *   • Both modes simultaneously (AP+STA)
 *   • Auto-detected by OS — NO custom driver installation needed
 *     Windows 10+: native UsbNcm.sys driver
 *     Linux:       native cdc_ncm kernel module
 *     macOS:       native NCM support
 *   • Web config portal at http://192.168.7.1 via the USB network
 *   • WiFi network scanning from the config page
 *   • LED status feedback (NeoPixel on GPIO 48)
 *   • Persistent config in NVS flash
 *
 * ── How it works ─────────────────────────────────────────────
 *   1. USB-NCM:  The ESP32-S3's native USB-OTG port presents itself
 *      as a CDC-NCM (Network Control Model) Ethernet adapter. The host
 *      OS loads its built-in NCM driver automatically — zero install.
 *
 *   2. Virtual Ethernet:  A subnet (192.168.7.0/24) is created between
 *      the PC and ESP32-S3. The ESP32 is the gateway (192.168.7.1) and
 *      runs a DHCP server to assign the PC an IP (192.168.7.2+).
 *
 *   3. STA (dongle) mode:  The ESP32-S3 connects to an upstream WiFi AP.
 *      NAPT (Network Address Port Translation) routes all PC traffic from
 *      the USB subnet through the WiFi uplink. To the PC, this is exactly
 *      like having a WiFi card — it gets internet through WiFi.
 *
 *   4. AP (hotspot) mode:  The ESP32-S3 broadcasts a SoftAP. The host PC
 *      can route its wired/cellular internet down through the USB link,
 *      and the ESP32-S3 re-broadcasts it as a WiFi hotspot. This is the
 *      "share internet as WiFi hotspot" feature.
 *
 *   5. AP+STA (dual):  Both modes simultaneously. The ESP32-S3 connects
 *      to upstream WiFi AND broadcasts a hotspot. NAPT handles routing
 *      for both the USB PC and the SoftAP WiFi clients.
 *
 * ── Board setup (Arduino IDE) ────────────────────────────────
 *   Board:          "ESP32S3 Dev Module"
 *   USB Mode:       "USB-OTG (TinyUSB)"   ← CRITICAL!
 *   USB CDC On Boot:"Disabled"
 *   PSRAM:          "OPI PSRAM" if your board has it
 *   Partition:      "Default 4MB" or larger
 *
 * ── Important ────────────────────────────────────────────────
 *   The companion  sdkconfig.defaults  file is REQUIRED.
 *   It enables:
 *     CONFIG_LWIP_IPV4_NAPT=y       (NAT routing)
 *     CONFIG_TINYUSB_NET_MODE_NCM=y (USB network adapter)
 *
 * ── Tested on ────────────────────────────────────────────────
 *   ESP32-S3-DevKitC-1 (N16R8) + Arduino-ESP32 core v3.x
 */

// ── Standard Arduino-ESP32 includes ──────────────────────────────────────────
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// ── ESP-IDF includes for NAPT and networking ─────────────────────────────────
#include <lwip/lwip_napt.h>
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "dhcpserver/dhcpserver.h"

// ── TinyUSB / USB-NCM ───────────────────────────────────────────────────────
// Arduino-ESP32 v3.x ships with esp_tinyusb component built-in.
// These headers provide the USB-NCM (Network Control Model) device class.
#include "tinyusb.h"
#include "tinyusb_net.h"

// ── RGB LED (ESP32-S3-DevKitC onboard NeoPixel) ─────────────────────────────
#define RGB_LED_PIN 48

// ── Forward declarations ─────────────────────────────────────────────────────
static void enableNAPT();
static void forwardDNStoClients();
static void setupUSBNetwork();
static void updateLED();

// ══════════════════════════════════════════════════════════════════════════════
// ██  Configuration
// ══════════════════════════════════════════════════════════════════════════════

Preferences prefs;

// WiFi STA config (connect to existing AP — the "dongle" function)
String sta_ssid     = "";
String sta_password = "";

// WiFi SoftAP config (broadcast hotspot — the "hotspot sharing" function)
String ap_ssid      = "ESP32_Dongle_AP";
String ap_password  = "12345678";

// Operating mode
enum DongleMode : uint8_t {
    MODE_STA_ONLY = 0,   // WiFi dongle only — connect PC to WiFi
    MODE_AP_ONLY  = 1,   // Hotspot dongle only — share PC's internet as WiFi
    MODE_AP_STA   = 2    // Both simultaneously (recommended)
};
DongleMode dongle_mode = MODE_AP_STA;

// ── Runtime state ────────────────────────────────────────────────────────────
static bool napt_active   = false;
static bool usb_link_up   = false;
static bool sta_connected = false;

// USB-NCM network interface handle (created by esp_tinyusb)
static esp_netif_t *usb_netif = NULL;

// USB MAC addresses — locally administered (bit 1 of first octet = 1)
// These MUST differ from the WiFi MACs
static const uint8_t usb_mac_device[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x01};
static const uint8_t usb_mac_host[6]   = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x02};


// ══════════════════════════════════════════════════════════════════════════════
// ██  USB-NCM Network Interface Setup
// ══════════════════════════════════════════════════════════════════════════════

/*
 * Initialize TinyUSB with the NCM (Network Control Model) device class.
 *
 * CDC-NCM is the MODERN standard for USB networking:
 *   ✓ Windows 10/11  — built-in UsbNcm.sys driver, auto-loaded
 *   ✓ Linux          — built-in cdc_ncm kernel module, auto-loaded
 *   ✓ macOS          — native NCM support
 *   ✓ No driver install needed on ANY platform
 *
 * After this runs, the host PC sees a new Ethernet adapter.
 * We assign IP 192.168.7.1 to the ESP32 side and run a DHCP server
 * so the PC gets 192.168.7.x automatically.
 */
static void setupUSBNetwork() {
    Serial.println("[USB] Initializing USB-NCM network adapter...");

    // ── Install TinyUSB driver ───────────────────────────────────────────────
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor        = NULL,   // Use Kconfig defaults
        .string_descriptor        = NULL,   // Use Kconfig defaults
        .string_descriptor_count  = 0,
        .external_phy             = false,
#if (TUD_OPT_HIGH_SPEED)
        .configuration_descriptor = NULL,
#endif
    };

    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        Serial.printf("[USB] FATAL: tinyusb_driver_install() failed: %s\n",
                      esp_err_to_name(err));
        return;
    }
    Serial.println("[USB] TinyUSB driver installed");

    // ── Initialize USB-NCM network layer ─────────────────────────────────────
    // This creates an esp_netif internally and binds it to TinyUSB's NCM class.
    // The host PC will see this as a standard Ethernet adapter.
    tinyusb_net_config_t net_cfg = {};
    memcpy(net_cfg.mac_addr, usb_mac_device, sizeof(usb_mac_device));

    err = tinyusb_net_init(TINYUSB_USBDEV_0, &net_cfg);
    if (err != ESP_OK) {
        Serial.printf("[USB] FATAL: tinyusb_net_init() failed: %s\n",
                      esp_err_to_name(err));
        Serial.println("[USB] Check: Is USB Mode set to 'USB-OTG (TinyUSB)' in Arduino IDE?");
        Serial.println("[USB] Check: Does sdkconfig.defaults have CONFIG_TINYUSB_NET_MODE_NCM=y?");
        return;
    }
    Serial.println("[USB] NCM network layer initialized");

    // ── Find the USB network interface ───────────────────────────────────────
    // The esp_tinyusb component creates a netif. Try known key names.
    const char *usb_keys[] = {"usb0", "USB_DEF", "tusb_ncm", NULL};
    for (int i = 0; usb_keys[i] != NULL; i++) {
        usb_netif = esp_netif_get_handle_from_ifkey(usb_keys[i]);
        if (usb_netif) {
            Serial.printf("[USB] Found netif with key: %s\n", usb_keys[i]);
            break;
        }
    }

    // Fallback: iterate all netifs
    if (!usb_netif) {
        esp_netif_t *netif = esp_netif_next(NULL);
        while (netif) {
            const char *key = esp_netif_get_ifkey(netif);
            const char *desc = esp_netif_get_desc(netif);
            Serial.printf("[USB] Found netif: key='%s' desc='%s'\n",
                          key ? key : "NULL", desc ? desc : "NULL");
            // Accept any netif that isn't WiFi STA or AP
            if (key && strstr(key, "WIFI") == NULL) {
                usb_netif = netif;
                Serial.printf("[USB] Using netif: %s\n", key);
                break;
            }
            netif = esp_netif_next(netif);
        }
    }

    if (!usb_netif) {
        Serial.println("[USB] ERROR: Could not find USB netif handle!");
        Serial.println("[USB] The USB-NCM adapter will appear to the host but");
        Serial.println("[USB] NAT routing and DHCP won't work.");
        return;
    }

    // ── Configure static IP and DHCP server on the USB interface ─────────────
    esp_netif_dhcps_stop(usb_netif);

    esp_netif_ip_info_t ip_info = {};
    IP4_ADDR(&ip_info.ip,      192, 168, 7, 1);
    IP4_ADDR(&ip_info.gw,      192, 168, 7, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_set_ip_info(usb_netif, &ip_info);

    // Start DHCP server — host PC will get 192.168.7.2+ automatically
    err = esp_netif_dhcps_start(usb_netif);
    if (err == ESP_OK) {
        Serial.println("[USB] DHCP server started on 192.168.7.0/24");
    } else {
        Serial.printf("[USB] DHCP server start failed: %s\n", esp_err_to_name(err));
    }

    usb_link_up = true;

    Serial.println("[USB] ════════════════════════════════════════════");
    Serial.println("[USB] USB WiFi Dongle ready!");
    Serial.println("[USB] Host PC will see a new network adapter.");
    Serial.println("[USB] Gateway IP: 192.168.7.1");
    Serial.println("[USB] ════════════════════════════════════════════");
}


// ══════════════════════════════════════════════════════════════════════════════
// ██  NAPT (NAT) + DNS Forwarding
// ══════════════════════════════════════════════════════════════════════════════

/*
 * Enable NAPT (Network Address Port Translation) on the USB and AP interfaces.
 *
 * This is the CORE of the WiFi dongle functionality:
 *   PC (192.168.7.x)  ──NAPT──►  WiFi STA (DHCP IP from router)  ──►  Internet
 *
 * Without NAPT, the PC can talk to the ESP32 but NOT reach the internet
 * through the WiFi connection.
 */
static void enableNAPT() {
    if (napt_active) return;

    // ── Enable NAPT on USB interface (routes PC traffic through WiFi) ────────
    if (usb_netif) {
        esp_err_t err = esp_netif_napt_enable(usb_netif);
        if (err == ESP_OK) {
            Serial.println("[NAT] NAPT enabled on USB → PC traffic routes through WiFi");
        } else {
            Serial.printf("[NAT] NAPT on USB failed: %s\n", esp_err_to_name(err));
        }
    }

    // ── Enable NAPT on SoftAP (routes WiFi client traffic through STA) ───────
    if (dongle_mode != MODE_STA_ONLY) {
        esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (ap_netif) {
            esp_err_t err = esp_netif_napt_enable(ap_netif);
            if (err == ESP_OK) {
                Serial.println("[NAT] NAPT enabled on AP → hotspot clients route through STA");
            } else {
                Serial.printf("[NAT] NAPT on AP failed: %s\n", esp_err_to_name(err));
            }
        }
    }

    napt_active = true;
    Serial.println("[NAT] ✓ NAT routing is ACTIVE — internet should work!");
}

/*
 * Forward the uplink DNS servers into the DHCP leases on both the USB
 * interface and the SoftAP interface. Without this, clients get DNS 0.0.0.0
 * and can't resolve hostnames → "no internet" even though packets route fine.
 */
static void forwardDNStoClients() {
    IPAddress dns_primary = WiFi.dnsIP(0);
    IPAddress dns_backup  = WiFi.dnsIP(1);

    if ((uint32_t)dns_primary == 0) {
        Serial.println("[DNS] No uplink DNS available yet — skipping");
        return;
    }

    Serial.printf("[DNS] Uplink DNS: %s", dns_primary.toString().c_str());
    if ((uint32_t)dns_backup != 0)
        Serial.printf(" / %s", dns_backup.toString().c_str());
    Serial.println();

    // Helper lambda: set DNS on a DHCP server netif
    auto setDNS = [&](esp_netif_t *netif, const char *label) {
        if (!netif) return;

        esp_netif_dhcps_stop(netif);

        esp_netif_dns_info_t dns_info = {};
        dns_info.ip.type = ESP_IPADDR_TYPE_V4;
        dns_info.ip.u_addr.ip4.addr = static_cast<uint32_t>(dns_primary);
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info);

        if ((uint32_t)dns_backup != 0) {
            dns_info.ip.u_addr.ip4.addr = static_cast<uint32_t>(dns_backup);
            esp_netif_set_dns_info(netif, ESP_NETIF_DNS_BACKUP, &dns_info);
        }

        // Enable DNS option in DHCP offers
        uint8_t offer_dns = OFFER_DNS;
        esp_netif_dhcps_option(netif,
            ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
            &offer_dns, sizeof(offer_dns));

        esp_netif_dhcps_start(netif);
        Serial.printf("[DNS] %s DHCP server updated with DNS\n", label);
    };

    // Set DNS on USB interface (for the host PC)
    setDNS(usb_netif, "USB");

    // Set DNS on SoftAP interface (for WiFi hotspot clients)
    if (dongle_mode != MODE_STA_ONLY) {
        esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        setDNS(ap_netif, "AP");
    }
}


// ══════════════════════════════════════════════════════════════════════════════
// ██  LED Status Indicator
// ══════════════════════════════════════════════════════════════════════════════

static void updateLED() {
    bool ap_on = (dongle_mode != MODE_STA_ONLY);

    if (sta_connected && ap_on) {
        neopixelWrite(RGB_LED_PIN, 15, 0, 15);  // Purple  = AP+STA active
    } else if (sta_connected) {
        neopixelWrite(RGB_LED_PIN, 0, 0, 15);   // Blue    = STA connected
    } else if (ap_on) {
        neopixelWrite(RGB_LED_PIN, 0, 15, 0);   // Green   = AP only
    } else {
        neopixelWrite(RGB_LED_PIN, 15, 8, 0);   // Amber   = USB up, no WiFi
    }
}


// ══════════════════════════════════════════════════════════════════════════════
// ██  WiFi Event Handler
// ══════════════════════════════════════════════════════════════════════════════

static void onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            sta_connected = true;
            Serial.printf("[WiFi] STA got IP: %s  GW: %s  DNS: %s\n",
                          WiFi.localIP().toString().c_str(),
                          WiFi.gatewayIP().toString().c_str(),
                          WiFi.dnsIP().toString().c_str());
            delay(200);   // let lwIP settle
            enableNAPT();
            forwardDNStoClients();
            updateLED();
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            sta_connected = false;
            napt_active   = false;
            Serial.println("[WiFi] STA disconnected — auto-reconnecting...");
            WiFi.reconnect();
            updateLED();
            break;

        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            Serial.println("[WiFi] Client joined SoftAP");
            break;

        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            Serial.println("[WiFi] Client left SoftAP");
            break;

        default:
            break;
    }
}


// ══════════════════════════════════════════════════════════════════════════════
// ██  NVS Configuration
// ══════════════════════════════════════════════════════════════════════════════

void loadConfig() {
    prefs.begin("dongle", true);
    sta_ssid     = prefs.getString("sta_ssid",  "");
    sta_password = prefs.getString("sta_pwd",   "");
    ap_ssid      = prefs.getString("ap_ssid",   "ESP32_Dongle_AP");
    ap_password  = prefs.getString("ap_pwd",    "12345678");
    dongle_mode  = (DongleMode)prefs.getUInt("mode", MODE_AP_STA);
    prefs.end();
}

void saveConfig() {
    prefs.begin("dongle", false);
    prefs.putString("sta_ssid", sta_ssid);
    prefs.putString("sta_pwd",  sta_password);
    prefs.putString("ap_ssid",  ap_ssid);
    prefs.putString("ap_pwd",   ap_password);
    prefs.putUInt("mode",       (uint32_t)dongle_mode);
    prefs.end();
}


// ══════════════════════════════════════════════════════════════════════════════
// ██  WiFi Network Scan
// ══════════════════════════════════════════════════════════════════════════════

String getWiFiScanOptions() {
    String opts = "";
    int n = WiFi.scanNetworks(false, false, false, 500);
    if (n <= 0) return opts;

    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        const char *enc = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "🔒";
        opts += "<option value='" + ssid + "'>" + ssid +
                " (" + String(rssi) + " dBm " + enc + ")</option>\n";
    }
    WiFi.scanDelete();
    return opts;
}


// ══════════════════════════════════════════════════════════════════════════════
// ██  Web Configuration Portal
// ══════════════════════════════════════════════════════════════════════════════

WebServer webServer(80);

// ── Helpers to render status page ────────────────────────────────────────────

String renderStatus() {
    bool connected = (WiFi.status() == WL_CONNECTED);
    bool ap_on     = (dongle_mode != MODE_STA_ONLY);

    String s = "";
    s += "<b>System Status</b><br>";
    s += "USB Link: <span class='" + String(usb_link_up ? "ok" : "fail") + "'>"
       + String(usb_link_up ? "✓ Connected (NCM)" : "✗ Not ready") + "</span><br>";
    s += "STA: <span class='" + String(connected ? "ok" : "fail") + "'>"
       + String(connected ? "✓ Connected" : "✗ Disconnected") + "</span><br>";
    if (connected) {
        s += "IP: " + WiFi.localIP().toString()
           + " &nbsp; GW: " + WiFi.gatewayIP().toString() + "<br>";
        s += "DNS: " + WiFi.dnsIP().toString() + "<br>";
    }
    s += "NAT: <span class='" + String(napt_active ? "ok" : "warn") + "'>"
       + String(napt_active ? "✓ Active" : "⏳ Inactive") + "</span><br>";
    if (ap_on) {
        s += "AP: <span class='ok'>✓ \"" + ap_ssid + "\"</span>"
           + " &nbsp; Clients: " + String(WiFi.softAPgetStationNum()) + "<br>";
    }

    unsigned long sec = millis() / 1000;
    char buf[32];
    snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
             sec / 86400, (sec % 86400) / 3600, (sec % 3600) / 60, sec % 60);
    s += "Uptime: " + String(buf);
    return s;
}

// ── HTML page ────────────────────────────────────────────────────────────────

static const char HTML_TEMPLATE[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>ESP32-S3 WiFi Dongle</title>
<style>
*{box-sizing:border-box;}
body{font-family:system-ui,-apple-system,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:20px;display:flex;justify-content:center;}
.c{background:#1e293b;padding:28px;border-radius:14px;width:100%;max-width:520px;box-shadow:0 10px 40px rgba(0,0,0,.5);}
h2{margin:0 0 20px;color:#38bdf8;font-size:22px;display:flex;align-items:center;gap:10px;}
h3{color:#94a3b8;border-bottom:1px solid #334155;padding-bottom:8px;margin:24px 0 12px;}
label{display:block;margin-top:14px;font-size:13px;color:#94a3b8;font-weight:600;}
input,select{width:100%;padding:10px 14px;margin-top:5px;border:1px solid #475569;border-radius:8px;background:#0f172a;color:#f1f5f9;font-size:14px;}
input:focus,select:focus{outline:none;border-color:#38bdf8;box-shadow:0 0 0 3px rgba(56,189,248,.15);}
.st{background:#0f172a;border:1px solid #334155;border-radius:10px;padding:14px 16px;margin-bottom:20px;font-size:12px;line-height:2;font-family:'SF Mono',monospace,monospace;}
.ok{color:#4ade80;font-weight:700;} .fail{color:#f87171;font-weight:700;} .warn{color:#fbbf24;font-weight:700;}
.hint{font-size:11px;color:#64748b;margin-top:4px;}
.badge{display:inline-block;padding:3px 12px;border-radius:99px;font-size:11px;font-weight:700;letter-spacing:.5px;}
.b-sta{background:#1e3a5f;color:#7dd3fc;} .b-ap{background:#14532d;color:#86efac;} .b-both{background:#3b0764;color:#d8b4fe;}
.btn{margin-top:24px;width:100%;padding:14px;background:#2563eb;color:#fff;border:none;border-radius:10px;font-weight:700;cursor:pointer;font-size:15px;transition:all .15s;}
.btn:hover{background:#1d4ed8;transform:translateY(-1px);box-shadow:0 4px 12px rgba(37,99,235,.4);}
.btn-scan{display:inline-block;margin-top:8px;padding:6px 16px;background:#334155;color:#94a3b8;border:1px solid #475569;border-radius:8px;font-size:12px;cursor:pointer;text-decoration:none;transition:all .15s;}
.btn-scan:hover{background:#475569;color:#e2e8f0;}
</style></head><body>
<div class='c'>
<h2>📶 USB WiFi Dongle <span class='badge %BADGE%'>%BADGE_TEXT%</span></h2>

<div class='st'>%STATUS%</div>

<form action='/save' method='POST'>

<h3>📡 Operating Mode</h3>
<select name='mode'>
<option value='0' %S0%>STA Only — USB WiFi dongle</option>
<option value='1' %S1%>AP Only — hotspot broadcaster</option>
<option value='2' %S2%>AP + STA — both (recommended)</option>
</select>
<div class='hint'>AP+STA lets you use WiFi <b>and</b> share a hotspot simultaneously.</div>

<h3>🌐 WiFi Station — connect to network</h3>
<label>WiFi Network (SSID)</label>
<input type='text' name='sta_ssid' value='%STA_SSID%' list='nets'
 placeholder='Select from scan or type manually'>
<datalist id='nets'>%SCAN%</datalist>
<a href='/scan' class='btn-scan'>🔍 Scan available networks</a>

<label>WiFi Password</label>
<input type='password' name='sta_pwd' value='%STA_PWD%' placeholder='Leave empty for open networks'>

<h3>📶 SoftAP — broadcast hotspot</h3>
<label>Hotspot Name (SSID)</label>
<input type='text' name='ap_ssid' value='%AP_SSID%' required>
<label>Hotspot Password (min 8 chars)</label>
<input type='password' name='ap_pwd' value='%AP_PWD%' minlength='8' required>

<button type='submit' class='btn'>💾 Save &amp; Reboot</button>
</form>
</div></body></html>
)rawhtml";

void fillPage(String &page, const String &scanOpts) {
    page.replace("%STA_SSID%", sta_ssid);
    page.replace("%STA_PWD%",  sta_password);
    page.replace("%AP_SSID%",  ap_ssid);
    page.replace("%AP_PWD%",   ap_password);
    page.replace("%SCAN%",     scanOpts);

    page.replace("%S0%", dongle_mode == MODE_STA_ONLY ? "selected" : "");
    page.replace("%S1%", dongle_mode == MODE_AP_ONLY  ? "selected" : "");
    page.replace("%S2%", dongle_mode == MODE_AP_STA   ? "selected" : "");

    const char *badge, *badgeText;
    switch (dongle_mode) {
        case MODE_STA_ONLY: badge = "b-sta";  badgeText = "STA";    break;
        case MODE_AP_ONLY:  badge = "b-ap";   badgeText = "AP";     break;
        default:            badge = "b-both"; badgeText = "AP+STA"; break;
    }
    page.replace("%BADGE%",      badge);
    page.replace("%BADGE_TEXT%", badgeText);
    page.replace("%STATUS%",     renderStatus());
}

void handleRoot() {
    String page = FPSTR(HTML_TEMPLATE);
    fillPage(page, "");
    webServer.send(200, "text/html", page);
}

void handleScan() {
    String scanOpts = getWiFiScanOptions();
    String page = FPSTR(HTML_TEMPLATE);
    fillPage(page, scanOpts);
    webServer.send(200, "text/html", page);
}

void handleSave() {
    sta_ssid     = webServer.arg("sta_ssid");
    sta_password = webServer.arg("sta_pwd");
    ap_ssid      = webServer.arg("ap_ssid");
    ap_password  = webServer.arg("ap_pwd");
    dongle_mode  = (DongleMode)webServer.arg("mode").toInt();

    saveConfig();

    webServer.send(200, "text/html",
        "<!DOCTYPE html><html><body style='background:#0f172a;color:#e2e8f0;"
        "font-family:system-ui;display:flex;justify-content:center;align-items:center;"
        "min-height:100vh;margin:0'>"
        "<div style='text-align:center'>"
        "<h2 style='color:#4ade80;font-size:28px'>✅ Saved!</h2>"
        "<p style='font-size:16px'>Rebooting in 2 seconds...</p>"
        "<p style='color:#94a3b8;font-size:13px;margin-top:20px'>"
        "After reboot, reconnect to the USB network adapter<br>"
        "and visit <b>http://192.168.7.1</b></p>"
        "</div></body></html>");

    delay(2000);
    ESP.restart();
}

void handleStatusJSON() {
    String json = "{";
    json += "\"usb\":" + String(usb_link_up ? "true" : "false") + ",";
    json += "\"sta\":" + String(sta_connected ? "true" : "false") + ",";
    json += "\"sta_ssid\":\"" + sta_ssid + "\",";
    json += "\"sta_ip\":\"" + (sta_connected ? WiFi.localIP().toString() : String("")) + "\",";
    json += "\"napt\":" + String(napt_active ? "true" : "false") + ",";
    json += "\"mode\":" + String((int)dongle_mode) + ",";
    json += "\"ap_ssid\":\"" + ap_ssid + "\",";
    json += "\"ap_clients\":" + String(WiFi.softAPgetStationNum()) + ",";
    json += "\"uptime\":" + String(millis());
    json += "}";
    webServer.send(200, "application/json", json);
}


// ══════════════════════════════════════════════════════════════════════════════
// ██  Setup
// ══════════════════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    delay(500);

    neopixelWrite(RGB_LED_PIN, 0, 0, 0);   // LED off

    Serial.println("\n");
    Serial.println("╔══════════════════════════════════════════════╗");
    Serial.println("║   ESP32-S3 USB WiFi Dongle                   ║");
    Serial.println("║   USB-NCM • STA + AP • Plug & Play           ║");
    Serial.println("╚══════════════════════════════════════════════╝");
    Serial.println();

    // ── 1. Load saved configuration ──────────────────────────────────────────
    loadConfig();

    Serial.printf("[CFG] Mode: %s\n",
        dongle_mode == MODE_STA_ONLY ? "STA Only (WiFi dongle)" :
        dongle_mode == MODE_AP_ONLY  ? "AP Only (hotspot)"  :
                                       "AP + STA (dual)");
    if (sta_ssid.length() > 0)
        Serial.printf("[CFG] STA target: \"%s\"\n", sta_ssid.c_str());
    if (dongle_mode != MODE_STA_ONLY)
        Serial.printf("[CFG] AP broadcast: \"%s\"\n", ap_ssid.c_str());

    // ── 2. Register WiFi event handler ───────────────────────────────────────
    WiFi.onEvent(onWiFiEvent);

    // ── 3. Initialize USB-NCM (the "USB network adapter" for the host PC) ────
    //    After this call, the host sees a new Ethernet adapter via USB.
    setupUSBNetwork();

    // ── 4. Configure WiFi mode ───────────────────────────────────────────────
    switch (dongle_mode) {
        case MODE_STA_ONLY:
            WiFi.mode(WIFI_STA);
            Serial.println("[WiFi] Mode: STA (WiFi dongle)");
            break;
        case MODE_AP_ONLY:
            WiFi.mode(WIFI_AP);
            Serial.println("[WiFi] Mode: AP (hotspot)");
            break;
        case MODE_AP_STA:
        default:
            WiFi.mode(WIFI_AP_STA);
            Serial.println("[WiFi] Mode: AP+STA (dual)");
            break;
    }

    // ── 5. Start SoftAP if needed (for hotspot / AP mode) ────────────────────
    if (dongle_mode != MODE_STA_ONLY) {
        IPAddress apIP(192, 168, 4, 1);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        WiFi.softAP(ap_ssid.c_str(), ap_password.c_str(), 6, 0, 8);
        Serial.printf("[AP]  Broadcasting \"%s\" on %s\n",
                      ap_ssid.c_str(), WiFi.softAPIP().toString().c_str());
    }

    // ── 6. Connect to upstream WiFi (STA mode) ───────────────────────────────
    if (dongle_mode != MODE_AP_ONLY && sta_ssid.length() > 0) {
        Serial.printf("[STA] Connecting to \"%s\"", sta_ssid.c_str());
        WiFi.begin(sta_ssid.c_str(), sta_password.c_str());
        WiFi.setAutoReconnect(true);

        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println(" ✓");
        } else {
            Serial.println("\n[STA] Timed out — will keep retrying in background");
        }
    } else if (dongle_mode != MODE_AP_ONLY) {
        Serial.println("[STA] No uplink SSID configured");
        Serial.println("[STA] Open http://192.168.7.1 from the PC to configure");
    }

    // ── 7. Start web configuration portal ────────────────────────────────────
    webServer.on("/",       HTTP_GET,  handleRoot);
    webServer.on("/scan",   HTTP_GET,  handleScan);
    webServer.on("/save",   HTTP_POST, handleSave);
    webServer.on("/status", HTTP_GET,  handleStatusJSON);
    webServer.begin();

    Serial.println();
    Serial.println("┌──────────────────────────────────────────────┐");
    Serial.println("│  📋 Config: http://192.168.7.1               │");
    if (dongle_mode != MODE_STA_ONLY)
    Serial.println("│  📋 Also:   http://192.168.4.1  (via WiFi)   │");
    Serial.println("│  📊 Status: http://192.168.7.1/status (JSON) │");
    Serial.println("└──────────────────────────────────────────────┘");
    Serial.println();

    updateLED();
}


// ══════════════════════════════════════════════════════════════════════════════
// ██  Loop
// ══════════════════════════════════════════════════════════════════════════════

void loop() {
    webServer.handleClient();

    // ── Periodic health check (every 30s) ────────────────────────────────────
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 30000) {
        lastCheck = millis();

        // Re-enable NAPT if WiFi reconnected but NAPT was lost
        if (sta_connected && !napt_active) {
            Serial.println("[Health] Re-enabling NAPT after reconnect...");
            enableNAPT();
            forwardDNStoClients();
        }

        updateLED();
    }
}

