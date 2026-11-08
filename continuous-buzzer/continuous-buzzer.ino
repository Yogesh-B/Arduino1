/*
 * Continuous Active Buzzer Sketch
 *
 * Hardware Wiring (Arduino Nano / Uno):
 * - Active Buzzer Positive (+) / Signal -> Digital Pin 7 (BUZZER_PIN)
 * - Active Buzzer Negative (-) / GND   -> Arduino GND
 *
 * This sketch turns the active buzzer ON immediately when the Arduino
 * is plugged in / powered on and keeps it sounding continuously.
 */

const int BUZZER_PIN = 7;  // Digital pin connected to the buzzer

// Set to HIGH for standard active-high buzzers.
// (Change to LOW if you are using an active-low buzzer module)
const int BUZZER_ON = HIGH;

void setup() {
  // Configure the buzzer pin as an output
  pinMode(BUZZER_PIN, OUTPUT);

  // Turn the buzzer ON continuously
  digitalWrite(BUZZER_PIN, BUZZER_ON);
}

void loop() {
  // Buzzer is kept ON continuously by the setup() call.
  // Nothing needed here.
}

