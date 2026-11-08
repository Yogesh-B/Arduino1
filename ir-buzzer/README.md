# IR Receiver with Active Buzzer

This sketch extends [`ir-decode1.ino`](../ir-decode1/ir-decode1.ino) by adding an **active buzzer** that provides audible feedback whenever an infrared remote signal is received.

---

## 1. Hardware Connections (Arduino Nano / Uno)

| Component | Pin | Arduino Nano Pin | Note |
|---|---|---|---|
| **IR Receiver Module** | VCC | 5V | Powers the sensor |
| | GND | GND | Ground |
| | OUT / Signal | **D8** | Configured as `RECV_PIN = 8` |
| **Active Buzzer** | Positive (+) / Signal | **D7** | Configured as `BUZZER_PIN = 7` |
| | Negative (-) / GND | GND | Ground |

> **Active vs. Passive Buzzer**:
> - An **active buzzer** has an internal oscillator. Supplying 5V (digital `HIGH`) immediately produces a steady tone (~2 kHz).
> - A **passive buzzer** requires alternating frequencies via PWM / `tone()`.
> - If your buzzer module has 3 pins (`VCC`, `GND`, `I/O`), connect `VCC` to 5V, `GND` to GND, and `I/O` (or `S`) to D7.

---

## 2. Features in `ir-buzzer.ino`

1. **Startup Check**: Beeps twice quickly when the Arduino powers on or resets, giving immediate audio confirmation that the buzzer is working properly.
2. **Keypress Feedback**: Emits a crisp 100 ms beep whenever a valid IR code is decoded.
3. **Repeat Filter**: Ignores `0xFFFFFFFF` (NEC hold/repeat code) so holding down a remote button won't cause annoying continuous buzzing.
4. **Serial Logging**: Prints the received hex codes to Serial Monitor (9600 baud) so you can identify each button on your remote.
5. **Custom Button Action Template**: Includes commented examples showing how to trigger different beep durations or patterns for specific remote buttons.

---

## 3. Active-Low Modules Note

If your buzzer sounds continuously and only turns off when a button is pressed, your module is **Active-Low**.
Simply change the `beep()` function to:
```cpp
void beep(int durationMs) {
  digitalWrite(BUZZER_PIN, LOW);   // ON for active-low
  delay(durationMs);
  digitalWrite(BUZZER_PIN, HIGH);  // OFF for active-low
}
```
and change the initial state in `setup()` to `digitalWrite(BUZZER_PIN, HIGH);`.

