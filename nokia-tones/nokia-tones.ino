/*
 * Nokia Tones on Arduino with UNIFIED SOFTWARE VOLUME CONTROL
 *
 * Pin Connections:
 * - Buzzer (+) / Signal -> Pin 7
 * - Buzzer (-) / GND    -> GND
 *
 * Both the SMS Tone and the Ringtone share the exact same VOLUME setting
 * and volume-scaling engine (perceptual quadratic duty-cycle modulation).
 */

const int BUZZER_PIN = 7;

// =========================================================================
// UNIFIED VOLUME SETTING (Controls BOTH SMS Tone and Ringtone)
// Range: 1 (whisper quiet) to 100 (full blast)
// Try 5 to 20 for comfortable desk listening!
// =========================================================================
int VOLUME = 40;

// --- Musical note frequencies ---
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_FS4 370
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_CS5 554
#define NOTE_D5  587
#define NOTE_E5  659

// Frequency used for the SMS tone:
// Kept in the same octave range (NOTE_E5 = 659 Hz) so its loudness and volume
// response perfectly match the ringtone melody!
const int SMS_NOTE = 1319;//NOTE_E5;

// Nokia Ringtone (Gran Vals) notes and durations (ms)
const int nokiaMelody[] = {
  NOTE_E5,  NOTE_D5,  NOTE_FS4, NOTE_GS4,
  NOTE_CS5, NOTE_B4,  NOTE_D4,  NOTE_E4,
  NOTE_B4,  NOTE_A4,  NOTE_CS4, NOTE_E4,
  NOTE_A4
};

const int noteDurations[] = {
  140, 140, 280, 280,
  140, 140, 280, 280,
  140, 140, 280, 280,
  560
};

// Unified tone generator with perceptual volume scaling
void playTone(int pin, unsigned int frequency, unsigned long durationMs, int volumePercent) {
  if (frequency == 0 || volumePercent <= 0) {
    delay(durationMs);
    return;
  }
  if (volumePercent > 100) volumePercent = 100;

  unsigned long periodMicros = 1000000UL / frequency;
  unsigned long maxHighTime = periodMicros / 2; // 50% square wave = max volume

  // Quadratic curve matching human hearing perception:
  // Gives smooth, audible volume changes across the entire 1-100 range
  unsigned long highTime = ((unsigned long)maxHighTime * volumePercent * volumePercent) / 10000UL;
  if (highTime < 2) highTime = 2;
  unsigned long lowTime = periodMicros - highTime;

  unsigned long startMillis = millis();
  while (millis() - startMillis < durationMs) {
    digitalWrite(pin, HIGH);
    delayMicroseconds(highTime);
    digitalWrite(pin, LOW);
    delayMicroseconds(lowTime);
  }
}

// =========================================================================
// 1. Nokia SMS Tone (... -- ...) - Uses Unified VOLUME
// =========================================================================
void playNokiaSmsTone() {
  const int dotLength  = 90;   // Short beep
  const int dashLength = 270;  // Long beep
  const int intraGap   = 90;   // Pause between dots/dashes
  const int letterGap  = 270;  // Pause between letters

  // 'S' = . . .
  playTone(BUZZER_PIN, SMS_NOTE, dotLength, VOLUME); delay(intraGap);
  playTone(BUZZER_PIN, SMS_NOTE, dotLength, VOLUME); delay(intraGap);
  playTone(BUZZER_PIN, SMS_NOTE, dotLength, VOLUME); delay(letterGap);

  // 'M' = - -
  playTone(BUZZER_PIN, SMS_NOTE, dashLength, VOLUME); delay(intraGap);
  playTone(BUZZER_PIN, SMS_NOTE, dashLength, VOLUME); delay(letterGap);

  // 'S' = . . .
  playTone(BUZZER_PIN, SMS_NOTE, dotLength, VOLUME); delay(intraGap);
  playTone(BUZZER_PIN, SMS_NOTE, dotLength, VOLUME); delay(intraGap);
  playTone(BUZZER_PIN, SMS_NOTE, dotLength, VOLUME);
}

// =========================================================================
// 2. Nokia Ringtone (Gran Vals) - Uses Unified VOLUME
// =========================================================================
void playNokiaRingtone() {
  int totalNotes = sizeof(nokiaMelody) / sizeof(nokiaMelody[0]);

  for (int thisNote = 0; thisNote < totalNotes; thisNote++) {
    int duration = noteDurations[thisNote];
    playTone(BUZZER_PIN, nokiaMelody[thisNote], duration, VOLUME);
    delay(duration * 0.30); // Pause between notes
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println(F("Nokia Tones Initialized"));
  Serial.print(F("Unified Volume: "));
  Serial.print(VOLUME);
  Serial.println(F("%"));

  // Play startup SMS tone
  Serial.println(F(">> Playing Startup SMS Tone..."));
  playNokiaSmsTone();
}

void loop() {
  delay(3000); // 3-second pause

  // 1. Play Nokia SMS Tone (... -- ...)
  // Serial.println(F(">> Playing Nokia SMS Tone..."));
  // playNokiaSmsTone();

  // delay(2500); // 2.5-second pause

  // 2. Play Nokia Ringtone
  Serial.println(F(">> Playing Nokia Ringtone..."));
  playNokiaRingtone();
}
