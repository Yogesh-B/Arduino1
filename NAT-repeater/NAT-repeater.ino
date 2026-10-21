/*
 * ESP32 NAT Repeater
 * ------------------
 * On first boot: connects to "ESP32_Repeater_Setup" AP (password: 12345678)
 * then visit http://192.168.4.1 to configure uplink + repeater settings.
 * Settings are saved to NVS and survive reboots.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <lwip/lwip_napt.h>

// ── Config stored in NVS ─────────────────────────────────────────────────────
Preferences prefs;

String sta_ssid     = "";
String sta_password = "";
String ap_ssid      = "ESP32_Repeater_Setup";
String ap_password  = "1231231231";

void loadConfig() {
    prefs.begin("rtr", true);  // read-only
    sta_ssid     = prefs.getString("sta_ssid",    "");
    sta_password = prefs.getString("sta_pwd",     "");
    ap_ssid      = prefs.getString("ap_ssid",     "ESP32_Repeater_Setup");
    ap_password  = prefs.getString("ap_pwd",      "1231231231");
    prefs.end();
}

void saveConfig(const String& ss, const String& sp,
                const String& as_, const String& ap) {
    prefs.begin("rtr", false);  // read-write
    prefs.putString("sta_ssid", ss);
    prefs.putString("sta_pwd",  sp);
    prefs.putString("ap_ssid",  as_);
    prefs.putString("ap_pwd",   ap);
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
.card{background:#242424;padding:25px;border-radius:10px;width:100%;max-width:420px;box-shadow:0 4px 15px rgba(0,0,0,.5);}
h2{margin-top:0;color:#3b82f6;}label{display:block;margin-top:12px;font-size:13px;color:#bbb;}
input{width:100%;padding:10px;margin-top:4px;border:1px solid #444;border-radius:6px;background:#333;color:#fff;box-sizing:border-box;}
button{margin-top:20px;width:100%;padding:12px;background:#2563eb;color:#fff;border:none;border-radius:6px;font-weight:bold;cursor:pointer;}
button:hover{background:#1d4ed8;}
</style></head><body>
<div class='card'>
<h2>Repeater Config</h2>
<form action='/save' method='POST'>
<h3>Uplink (Master AP)</h3>
<label>Target SSID</label><input type='text' name='sta_ssid' value='%STA_SSID%' required>
<label>Target Password</label><input type='password' name='sta_pwd' value='%STA_PWD%'>
<h3>Repeater (Local AP)</h3>
<label>Repeater SSID</label><input type='text' name='ap_ssid' value='%AP_SSID%' required>
<label>Repeater Password (min 8 chars)</label><input type='password' name='ap_pwd' value='%AP_PWD%' minlength='8' required>
<button type='submit'>Save &amp; Reboot</button>
</form></div></body></html>
)rawhtml";

void handleRoot() {
    String page = FPSTR(HTML_PAGE);
    page.replace("%STA_SSID%", sta_ssid);
    page.replace("%STA_PWD%",  sta_password);
    page.replace("%AP_SSID%",  ap_ssid);
    page.replace("%AP_PWD%",   ap_password);
    server.send(200, "text/html", page);
}

void handleSave() {
    String ss  = server.arg("sta_ssid");
    String sp  = server.arg("sta_pwd");
    String as_ = server.arg("ap_ssid");
    String ap  = server.arg("ap_pwd");

    saveConfig(ss, sp, as_, ap);
    server.send(200, "text/html",
        "<html><body><h2>Saved! Rebooting in 2 seconds...</h2></body></html>");
    delay(2000);
    ESP.restart();
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n=== ESP32 NAT Repeater ===");

    loadConfig();

    // Always start SoftAP for config portal
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),
        IPAddress(192, 168, 4, 1),
        IPAddress(255, 255, 255, 0)
    );
    WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());
    Serial.printf("AP started  →  SSID: %s   IP: %s\n",
                  ap_ssid.c_str(), WiFi.softAPIP().toString().c_str());

    // Connect to uplink if credentials exist
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
            Serial.printf("\nSTA connected  →  IP: %s\n",
                          WiFi.localIP().toString().c_str());

            // Enable NAPT so AP clients route through STA
            delay(500);  // let lwIP settle
            ip_napt_enable(WiFi.softAPIP(), 1);
            Serial.println("NAPT enabled  →  NAT routing active");
        } else {
            Serial.println("\nUplink connection failed — running in AP-only mode");
        }
    } else {
        Serial.println("No uplink configured — open http://192.168.4.1 to set up");
    }

    // Start web config portal
    server.on("/",      HTTP_GET,  handleRoot);
    server.on("/save",  HTTP_POST, handleSave);
    server.begin();
    Serial.println("Config portal →  http://192.168.4.1");
}

void loop() {
    server.handleClient();
}
