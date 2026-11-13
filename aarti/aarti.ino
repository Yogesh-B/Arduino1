/*
 * Aarti Tone for Arduino with Software Volume Control
 * Traditional Aarti melody in the classic devotional meter
 *
 * Hardware Wiring (Arduino Uno / Nano / ESP32 / etc.):
 * - Passive / Piezo Buzzer Positive (+) -> Digital Pin 7 (BUZZER_PIN)
 * - Passive / Piezo Buzzer Negative (-) -> Arduino GND
 *
 * Features:
 * - Authentic traditional Aarti sargam and phrasing
 * - Sthayi (Chorus) + Antara (Verse) + Refrain
 * - Perceptual volume scaling (1 to 100)
 * - Optional temple bell chime intro
 */

const int BUZZER_PIN = 7;

// =========================================================================
// VOLUME SETTING  (1 = whisper quiet, 100 = full blast)
// =========================================================================
int VOLUME = 100;

const bool ENABLE_TEMPLE_BELL = true;

// --- Musical Note Frequencies (Hz) ---
// Sargam mapping: Sa=C5, Re=D5, Ga=E5, Ma=F5, Pa=G5, Dha=A5, Ni=B5
// Lower octave:  .Dha=A4, .Ni=B4

#define REST  0

#define Lo_D  220   // .Dha (lower octave Dha = A3 — used only in cadence)
#define Lo_N  247   // .Ni  (lower octave Ni  = B3)
#define Lo_Dh 440   // .Dha (A4)
#define Lo_Ni 494   // .Ni  (B4)

#define Sa  523     // C5
#define Re  587     // D5
#define Ga  659     // E5
#define Ma  698     // F5
#define Pa  784     // G5
#define Dha 880     // A5
#define Ni  988     // B5

// Bell chime frequencies
#define BELL_HI 2093
#define BELL_LO 1568

// Beat unit in ms — controls overall tempo
// Increase for slower, decrease for faster
const int B = 300;   // one beat = 300 ms  (~100 BPM aarti pace)

// Note structure
struct Note {
  int freq;
  int dur;   // duration in ms
};

// =========================================================================
// AARTI MELODY
// Sargam (verified cross-reference from multiple sources):
//
// STHAYI (Chorus):
// Line 1:  S - | S - | S S S - | .N S R -
// Line 2:  R G | M - | P P | D P M - | G M R -
// Line 3:  R G R - | G M - | M G - | R G R S -
// Line 4:  .N S R - | G R S - | .N S .N .D -
// Line 5:  R - R - | R R G R | S .N S -
//
// ANTARA (Verse):
// Line 6:  R S - S .N - | R S - S .N - | R S S -
// Line 7:  P D P M - | G - | R S - R - -
// Line 8:  P P - P D P M - | G - | R S - R - -
// Line 9:  R G - R G M - | M G - | R G R S -
// Line 10: .N S R - | R G R - | S .N - S .N .D -
// Line 11: R - R - | R R G R | S .N S -
// =========================================================================
const Note aartiMelody[] = {

  // ===================== STHAYI (CHORUS) =====================

  // Line 1: S - | S - | S S S - | .N S R -
  //         "Om    Jai    Jag-dish  Ha-re"
  { Sa,  B*2 },           // S -       (Om)
  { Sa,  B*2 },           // S -       (Jai)
  { Sa,  B },             // S         (Jag-)
  { Sa,  B },             // S         (-dish)
  { Sa,  B*2 },           // S -       (Ha-)
  { Lo_Ni, B },           // .N        (-re)
  { Sa,  B },             // S
  { Re,  B*2 },           // R -
  { REST, B/2 },

  // Line 2: R G | M - | P P | D P M - | G M R -
  //         "Swa-mi  Jai  Jag-dish  Ha--------re"
  { Re,  B },             // R         (Swa-)
  { Ga,  B },             // G         (-mi)
  { Ma,  B*2 },           // M -       (Jai)
  { Pa,  B },             // P         (Jag-)
  { Pa,  B },             // P         (-dish)
  { Dha, B },             // D         (Ha-)
  { Pa,  B },             // P
  { Ma,  B*2 },           // M -
  { Ga,  B },             // G
  { Ma,  B },             // M
  { Re,  B*2 },           // R -
  { REST, B/2 },

  // Line 3: R G R - | G M - | M G - | R G R S -
  //         "Bhakt ja-no   ke     san-kat"
  { Re,  B },             // R
  { Ga,  B },             // G
  { Re,  B*2 },           // R -
  { Ga,  B },             // G
  { Ma,  B*2 },           // M -
  { Ma,  B },             // M
  { Ga,  B*2 },           // G -
  { Re,  B },             // R
  { Ga,  B },             // G
  { Re,  B },             // R
  { Sa,  B*2 },           // S -
  { REST, B/2 },

  // Line 4: .N S R - | G R S - | .N S .N .D -
  //         "Kshan mein  door   ka------re"
  { Lo_Ni, B },           // .N
  { Sa,  B },             // S
  { Re,  B*2 },           // R -
  { Ga,  B },             // G
  { Re,  B },             // R
  { Sa,  B*2 },           // S -
  { Lo_Ni, B },           // .N
  { Sa,  B },             // S
  { Lo_Ni, B },           // .N
  { Lo_Dh, B*2 },         // .D -
  { REST, B/2 },

  // Line 5: R - R - | R R G R | S .N S -
  //         "Om  Jai   Jag-dish  Ha-re"
  { Re,  B*2 },           // R -
  { Re,  B*2 },           // R -
  { Re,  B },             // R
  { Re,  B },             // R
  { Ga,  B },             // G
  { Re,  B },             // R
  { Sa,  B },             // S
  { Lo_Ni, B },           // .N
  { Sa,  B*2 },           // S -
  { REST, B },

  // ===================== ANTARA (VERSE) =====================

  // Line 6: R S - S .N - | R S - S .N - | R S S -
  //         "Jo  dhya-ve    fal pa-------ve"
  { Re,  B },             // R
  { Sa,  B*2 },           // S -
  { Sa,  B },             // S
  { Lo_Ni, B*2 },         // .N -
  { Re,  B },             // R
  { Sa,  B*2 },           // S -
  { Sa,  B },             // S
  { Lo_Ni, B*2 },         // .N -
  { Re,  B },             // R
  { Sa,  B },             // S
  { Sa,  B*2 },           // S -
  { REST, B/2 },

  // Line 7: P D P M - | G - | R S - R - -
  //         "Dukh bin-se    man   ka"
  { Pa,  B },             // P
  { Dha, B },             // D
  { Pa,  B },             // P
  { Ma,  B*2 },           // M -
  { Ga,  B*2 },           // G -
  { Re,  B },             // R
  { Sa,  B*2 },           // S -
  { Re,  B*3 },           // R - -
  { REST, B/2 },

  // Line 8: P P - P D P M - | G - | R S - R - -
  //         "Swa-mi   dukh bin-se   man  ka"
  { Pa,  B },             // P
  { Pa,  B*2 },           // P -
  { Pa,  B },             // P
  { Dha, B },             // D
  { Pa,  B },             // P
  { Ma,  B*2 },           // M -
  { Ga,  B*2 },           // G -
  { Re,  B },             // R
  { Sa,  B*2 },           // S -
  { Re,  B*3 },           // R - -
  { REST, B/2 },

  // Line 9: R G - R G M - | M G - | R G R S -
  //         "Sukh sam-pa-ti  ghar   aa---ve"
  { Re,  B },             // R
  { Ga,  B*2 },           // G -
  { Re,  B },             // R
  { Ga,  B },             // G
  { Ma,  B*2 },           // M -
  { Ma,  B },             // M
  { Ga,  B*2 },           // G -
  { Re,  B },             // R
  { Ga,  B },             // G
  { Re,  B },             // R
  { Sa,  B*2 },           // S -
  { REST, B/2 },

  // Line 10: .N S R - | R G R - | S .N - S .N .D -
  //          "Kasht mi-te   tan   ka"
  { Lo_Ni, B },           // .N
  { Sa,  B },             // S
  { Re,  B*2 },           // R -
  { Re,  B },             // R
  { Ga,  B },             // G
  { Re,  B*2 },           // R -
  { Sa,  B },             // S
  { Lo_Ni, B*2 },         // .N -
  { Sa,  B },             // S
  { Lo_Ni, B },           // .N
  { Lo_Dh, B*2 },         // .D -
  { REST, B/2 },

  // Line 11: R - R - | R R G R | S .N S -   (Refrain)
  //          "Om  Jai   Jag-dish  Ha-re"
  { Re,  B*2 },           // R -
  { Re,  B*2 },           // R -
  { Re,  B },             // R
  { Re,  B },             // R
  { Ga,  B },             // G
  { Re,  B },             // R
  { Sa,  B },             // S
  { Lo_Ni, B },           // .N
  { Sa,  B*3 },           // S - -  (final hold)
};

const int TOTAL_NOTES = sizeof(aartiMelody) / sizeof(aartiMelody[0]);

// =========================================================================
// Tone generator with perceptual quadratic volume scaling
// =========================================================================
void playTone(int pin, unsigned int frequency, unsigned long durationMs, int volumePercent) {
  if (frequency == 0 || volumePercent <= 0) {
    delay(durationMs);
    return;
  }
  if (volumePercent > 100) volumePercent = 100;

  unsigned long periodMicros = 1000000UL / frequency;
  unsigned long maxHighTime = periodMicros / 2;

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

// Temple bell chime
void playTempleBell() {
  playTone(BUZZER_PIN, BELL_HI, 100, VOLUME);
  delay(60);
  playTone(BUZZER_PIN, BELL_LO, 150, VOLUME);
  delay(150);
  playTone(BUZZER_PIN, BELL_HI, 100, VOLUME);
  delay(60);
  playTone(BUZZER_PIN, BELL_LO, 300, VOLUME);
  delay(350);
}

// Play the complete Aarti melody
void playAartiTone() {
  if (ENABLE_TEMPLE_BELL) {
    Serial.println(F(">> Temple Bell..."));
    playTempleBell();
  }

  Serial.println(F(">> Playing Aarti..."));

  for (int i = 0; i < TOTAL_NOTES; i++) {
    int f = aartiMelody[i].freq;
    int d = aartiMelody[i].dur;

    if (f == REST) {
      delay(d);
    } else {
      // 90% sound, 10% silence for clean note separation
      unsigned long snd = (unsigned long)d * 90 / 100;
      unsigned long gap = d - snd;
      playTone(BUZZER_PIN, f, snd, VOLUME);
      delay(gap);
    }
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println(F("Aarti Tone Ready"));
  Serial.print(F("Volume: "));
  Serial.print(VOLUME);
  Serial.println(F("%"));

  playAartiTone();
}

void loop() {
  delay(3000);
  playAartiTone();
}
