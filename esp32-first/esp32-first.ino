// ESP32-S3 N16R8 Built-in RGB Test
#define RGB_PIN 48 // Common pin: 48 (or 38 on some board clones)

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n--- ESP32-S3 N16R8 Hardware Test ---");
  Serial.printf("Chip Model: %s (Rev %d)\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("Flash Size: %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  
  if (psramInit()) {
    Serial.printf("PSRAM Initialized: %u MB Available\n", ESP.getPsramSize() / (1024 * 1024));
  } else {
    Serial.println("PSRAM Not Detected");
  }
}

void loop() {
  // Red (Red, Green, Blue: 0-255)
  Serial.println("LED: Red");
  neopixelWrite(RGB_PIN, 64, 0, 0); 
  delay(1000);

  // Green
  Serial.println("LED: Green");
  neopixelWrite(RGB_PIN, 0, 64, 0);
  delay(1000);

  // Blue
  Serial.println("LED: Blue");
  neopixelWrite(RGB_PIN, 0, 0, 64);
  delay(1000);

  // OFF (RGB: 0, 0, 0)
  Serial.println("LED: Off");
  neopixelWrite(RGB_PIN, 0, 0, 0);
  delay(1000);
}