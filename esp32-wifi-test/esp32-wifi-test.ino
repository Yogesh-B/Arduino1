#include "WiFi.h"

#define RGB_PIN 48 // Use GPIO 48 (or 38 if on an alternate board variant)

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==================================");
  Serial.println("  ESP32-S3 Wi-Fi Hardware Scan    ");
  Serial.println("==================================");

  // Set Wi-Fi to station mode and disconnect from any prior network
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.printf("Base MAC Address: %s\n", WiFi.macAddress().c_str());
  Serial.println("Starting network scan...\n");
}

void loop() {
  // Indicate scanning by flashing Cyan
  neopixelWrite(RGB_PIN, 0, 32, 32);

  int n = WiFi.scanNetworks();

  // Indicate scan complete by turning LED Off
  neopixelWrite(RGB_PIN, 0, 0, 0);

  if (n == 0) {
    Serial.println("No networks found.");
  } else {
    Serial.printf("Scan Complete: %d networks found\n", n);
    Serial.println("-------------------------------------------------------------------");
    Serial.printf("%-4s | %-32s | %-6s | %-17s | %s\n", "No.", "SSID", "RSSI", "BSSID (MAC)", "Encryption");
    Serial.println("-------------------------------------------------------------------");

    for (int i = 0; i < n; ++i) {
      String encType;
      switch (WiFi.encryptionType(i)) {
        case WIFI_AUTH_OPEN: encType = "Open"; break;
        case WIFI_AUTH_WEP: encType = "WEP"; break;
        case WIFI_AUTH_WPA_PSK: encType = "WPA"; break;
        case WIFI_AUTH_WPA2_PSK: encType = "WPA2"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: encType = "WPA/WPA2"; break;
        case WIFI_AUTH_WPA2_ENTERPRISE: encType = "WPA2-Enterprise"; break;
        case WIFI_AUTH_WPA3_PSK: encType = "WPA3"; break;
        default: encType = "Unknown"; break;
      }

      Serial.printf("%-4d | %-32.32s | %-4d dBm | %-17s | %s\n",
                    i + 1,
                    WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i),
                    WiFi.BSSIDstr(i).c_str(),
                    encType.c_str());
      delay(10);
    }
    Serial.println("-------------------------------------------------------------------\n");
  }

  // Delete scan results from RAM buffer
  WiFi.scanDelete();

  // Pause for 10 seconds before next scan
  delay(10000);
}