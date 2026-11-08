/*
 * IR Receiver with Active Buzzer Sound
 * Created from ir-decode1 sketch
 *
 * Hardware Wiring (e.g. Arduino Nano / Uno):
 * -------------------------------------------------------------
 * IR Receiver:
 *   - VCC    -> Arduino 5V
 *   - GND    -> Arduino GND
 *   - OUT / Signal -> Digital Pin 8 (RECV_PIN)
 *
 * Active Buzzer:
 *   - Positive (+) / Signal -> Digital Pin 7 (BUZZER_PIN)
 *   - Negative (-) / GND    -> Arduino GND
 *
 * (Note: An active buzzer has an internal oscillator; giving it
 *  HIGH turns the sound ON, and LOW turns it OFF.)
 */

#include <IRremote.h>

int RECV_PIN = 8;
int BUZZER_PIN = 7;  // Pin connected to the active buzzer

IRrecv irrecv(RECV_PIN);
decode_results results;

// Helper function to produce a clean beep of given duration in ms
void beep(int durationMs) {
  digitalWrite(BUZZER_PIN, HIGH);  // Turn buzzer ON
  delay(durationMs);
  digitalWrite(BUZZER_PIN, LOW);   // Turn buzzer OFF
}

void setup() {
  Serial.begin(9600);

  // Set buzzer pin as output and turn it off initially
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  irrecv.enableIRIn();  // Start the receiver

  Serial.println(F("--- IR Receiver with Active Buzzer Initialized ---"));

  // Power-on confirmation: two quick beeps to verify buzzer works
  beep(80);
  delay(80);
  beep(80);
}

void loop() {
  if (irrecv.decode(&results)) {
    // Print decoded hex value to Serial Monitor
    Serial.print(F("IR Code: 0x"));
    Serial.println(results.value, HEX);

    // Beep whenever an IR code is received.
    // Filter out 0xFFFFFFFF (repeat code sent when holding a remote button)
    // so it doesn't buzz continuously while holding a key.
    if (results.value != 0xFFFFFFFF) {
      beep(100);  // 100ms audible feedback beep
    }

    /*
     * OPTIONAL: Trigger specific sounds or actions for specific buttons
     * You can check the Serial Monitor for your remote's button codes:
     *
     * switch (results.value) {
     *   case 0xFFA25D: // Example: Power button -> Double beep
     *     beep(150); delay(100); beep(150);
     *     break;
     *   case 0xFF629D: // Example: Mode button -> Long beep
     *     beep(400);
     *     break;
     * }
     */

    irrecv.resume(); // Receive the next value
  }
}

