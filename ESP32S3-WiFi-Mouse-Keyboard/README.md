# 🖱️ ESP32-S3 WiFi Dongle for Keyboard & Mouse

Turns an **ESP32-S3** into a wireless USB trackpad and remote keyboard. Plug the ESP32-S3 into any PC (Windows, Linux, or macOS) via USB, connect your smartphone or tablet to its WiFi, and navigate your PC with an ultra-responsive touch trackpad and mobile keyboard!

---

## ✨ Features

- **Driverless USB HID:** Plugs into any PC as a standard USB Composite Mouse & Keyboard device. No companion drivers or software needed on the computer.
- **Modern Web Dashboard:** Styled with the dark aesthetic from the SD Card FTP sketch (`#1a1a2e`, `#16213e`, `#e94560`, clean card panels, live status badges).
- **Multi-Touch Trackpad:**
  - **1-Finger Drag:** Smooth cursor navigation with velocity-sensitive acceleration.
  - **1-Finger Tap:** Left Click.
  - **1-Finger Double-Tap & Hold:** Click-and-drag (select text, move windows, drag files).
  - **2-Finger Tap:** Right Click (context menu).
  - **2-Finger Drag / Swipe:** Mouse Scroll Wheel.
  - **On-Screen Mouse Buttons:** Physical Left, Middle, and Right buttons for thumb operation.
- **Fullscreen Mode:**
  - One-tap fullscreen (`⛶ Fullscreen`) expands the trackpad across the phone's entire display ($100\text{vw} \times 100\text{vh}$).
  - Floating translucent overlays for quick access to the pop-up keyboard, sensitivity, and thumb click pads.
- **Mobile Keyboard & Live Keystroke Forwarding:**
  - **Pop-Up Native Keyboard:** Tap to focus the input box and summon your phone's native on-screen keyboard (iOS / Android). Keystrokes are forwarded live to the PC.
  - **Send Full Text / Paste:** Type or paste entire sentences, passwords, or shell commands and click **"Type Text on PC"**.
  - **Special Desktop Keys:** Dedicated touch buttons for `Esc`, `Tab`, `Backspace`, `Enter`, `Delete`, `Space`, and Arrow keys (`▲`, `◀`, `▼`, `▶`).
  - **Sticky Modifiers & Hotkeys:** `Ctrl`, `Alt`, `Shift`, `Win/Cmd` with one-tap shortcuts like `Ctrl+C`, `Ctrl+V`, `Ctrl+Z`, `Alt+Tab`.
- **AP & Station Mode Configuration:**
  - Switch between standalone Access Point hotspot mode or Station mode (join your home/office WiFi router).
  - Built-in **mDNS** (`http://esp-mouse.local`).
  - Automatic fallback to AP mode if the router cannot be reached within 20 seconds.
- **Low-Latency WebSocket Streaming:**
  - Persistent sub-5ms WebSocket connection on port 81 (RFC 6455 compliant, zero external library dependencies).

---

## 🛠️ Hardware Requirements & Wiring

1. **Board:** Any ESP32-S3 development board (e.g., ESP32-S3-DevKitC-1, N16R8, or USB dongle variant).
2. **USB Connection to PC:**
   - Connect the PC to the **USB (Native USB-OTG)** port on the ESP32-S3 (GPIO 19 for D-, GPIO 20 for D+).
   - *Note:* Many DevKit boards have two USB ports: **UART** (for serial programming) and **USB** (native USB-OTG). The host PC must be connected to the **USB** port to receive mouse and keyboard events.

---

## 💻 Arduino IDE Setup

1. **Board:** Select `Tools` $\rightarrow$ `Board` $\rightarrow$ `esp32` $\rightarrow$ **`ESP32S3 Dev Module`**.
2. **USB Mode:** `Tools` $\rightarrow$ `USB Mode` $\rightarrow$ **`Hardware CDC and JTAG`** (or `USB-OTG (TinyUSB)`).
3. **USB CDC On Boot:** `Tools` $\rightarrow$ `USB CDC On Boot` $\rightarrow$ **`Disabled`** (or `Enabled` if you want debug messages over the same USB port).
4. **Flash Size:** `16MB` (or matching your board module, e.g. 4MB / 8MB).
5. **PSRAM:** `OPI PSRAM` (if equipped on N16R8, otherwise Disabled).
6. Hit **Upload**.

---

## 📱 How to Use

### 1. First Boot (Access Point Mode)
1. Plug the ESP32-S3 into your PC's USB port.
2. The onboard LED will turn **Cyan**, indicating AP mode is active.
3. On your smartphone or laptop, connect to the WiFi network:
   - **SSID:** `ESP32-AirInput`
   - **Password:** `12345678`
4. Open your browser and navigate to:
   - **`http://192.168.4.1`** or **`http://esp-mouse.local`**

### 2. Trackpad & Fullscreen
- Move your finger over the trackpad card to move the PC mouse pointer.
- Tap the **`⛶ Fullscreen`** button in the card header. The trackpad will expand to the full display.
- In fullscreen, tap the **`⌨ Pop Keyboard`** button in the top left overlay to quickly type, or **`✕ Exit`** in the top right to return to the dashboard.

### 3. Keyboard Input
- Navigate to the **⌨️ Keyboard** tab.
- Tap into the **Live Mobile Typing** input box to open your phone's native keyboard.
- Use the **Special Keys** and **Modifiers** buttons at the bottom for desktop shortcuts (`Esc`, `Tab`, `Ctrl+C`, `Alt+Tab`, Arrow keys).

### 4. Switch to Station (Home Router) Mode
1. Go to the **⚙️ Settings** tab.
2. Select **Station (join router)**.
3. Enter your WiFi network SSID and Password.
4. (Optional) Customize the device hostname (e.g., `my-mouse` for `http://my-mouse.local`).
5. Click **💾 Save & Restart**.
6. The ESP32-S3 will reboot and join your local router. The LED will turn **Green**, and you can now open `http://esp-mouse.local` from any device connected to the same WiFi network!

---

## 📂 Project Structure

```
ESP32S3-WiFi-Mouse-Keyboard/
├── ESP32S3-WiFi-Mouse-Keyboard.ino  # Main Arduino sketch & event routing
├── WebSocketServer.h                # Standalone RFC 6455 WebSocket engine (port 81)
├── WebPage.h                        # Single-page HTML/CSS/JS dashboard (PROGMEM)
└── README.md                        # Documentation and guide
```
