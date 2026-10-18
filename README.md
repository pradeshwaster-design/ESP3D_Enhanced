# ESP3D-Enhanced — Remote 3D Printer Control & Monitoring

<p align="center">
  <b>WiFi Bridge + 2-Way Telegram Bot for Ender 3 + SKR Mini E3 V3.0 + ESP32-WROOM-DA</b>
</p>

<p align="center">
  <a href="https://github.com/pradeshwaster-design/ESP3D-Enhanced-/commits/main"><img src="https://img.shields.io/badge/Version-v7.0-blue?style=for-the-badge"/></a>
  <a href="https://github.com/pradeshwaster-design/ESP3D-Enhanced-/commits/main"><img src="https://img.shields.io/badge/Last%20Commit-main-green?style=for-the-badge"/></a>
  <a href="https://opensource.org/licenses/GPL-3.0"><img src="https://img.shields.io/badge/License-GPLv3-yellow?style=for-the-badge"/></a>
</p>

---

## 📖 Overview

**ESP3D-Enhanced** is open-source firmware for **ESP8266**, **ESP8285**, and **ESP32** microcontrollers that adds Wi-Fi connectivity, web control, and remote monitoring to 3D printers, CNC machines, and laser engravers.

### Tested Hardware Setup
| Component | Model |
|-----------|-------|
| 3D Printer | Creality Ender 3 |
| Controller Board | SKR Mini E3 V3.0 |
| WiFi Bridge | ESP32-WROOM-DA (240MHz) |
| Firmware | Marlin 2.1.x |
| Flash Scheme | Minimal SPIFFS (1.9MB APP + OTA / 190KB SPIFFS) |

Monitor prints remotely, upload G-code files from Telegram, tune speed/flow/fan live, control your printer via 2-way Telegram bot, and view everything on the ESP3D web dashboard — all from your phone or PC.

## ✨ Features

### 📱 2-Way Telegram Bot
Send commands from anywhere and get instant replies:

| Command | Action | G-Code Sent |
|---------|--------|-------------|
| `/status` | Live print progress, %, filename, temps, Z, time left | Serial read |
| `/pos` | Current toolhead coordinates | `M114` |
| `/display` | Everything on the printer LCD screen | Serial read |
| `/temp` | Live hotend & bed temperatures | Serial read |
| `/files` | List `.gcode` files on SD card | `M21`, `M20` |
| `/print <num\|name>` | Select and start printing a file | `M23`, `M24` |
| `/pause` | Safely pause mid-layer | `M25` |
| `/resume` | Resume from paused state | `M108` |
| `/stop` | Cancel and abort current print | `M108`, `M524`, `M104 S0`, `M140 S0` |
| `/emergency` | Immediate emergency kill | `M112` |
| `/cooldown` | Turn off hotend & bed | `M104 S0`, `M140 S0` |
| `/preheat pla` | Preheat to PLA temps (200°C/60°C) | `M104 S200`, `M140 S60` |
| `/preheat petg` | Preheat to PETG temps (240°C/75°C) | `M104 S240`, `M140 S75` |
| `/temp <deg>` | Set hotend temperature (max 260°C) | `M104 S<deg>` |
| `/bed <deg>` | Set bed temperature (max 100°C) | `M140 S<deg>` |
| `/speed <pct>` | Print speed override (10–300%) | `M220 S<pct>` |
| `/flow <pct>` | Extrusion multiplier (50–150%) | `M221 S<pct>` |
| `/fan <pct>` | Part cooling fan (0–100%) | `M106 S<pwm>` |
| `/zoffset <+/-mm>` | Live Z-babystepping (-2 to +2 mm) | `M290 Z<val>` |
| `/load` | Load filament (hotend ≥170°C) | `G91`, `M83`, `G1 E60 F150`, `G90` |
| `/unload` | Unload filament (hotend ≥170°C) | `G91`, `M83`, `G1 E10 F200`, ... |
| `/continue` | Continue from LCD prompt | `M876 S1`, `M108` |
| `/purge` | Purge more filament | `M876 S0` |
| `/info` | Full system diagnostics report | Serial read |
| `/help` | Show full command list | — |

### 📁 Direct G-Code Upload from Telegram (V7)
Upload `.gcode` files directly from Telegram to the printer's onboard SD card:
1. Send any `.gcode` file in your Telegram chat with the bot
2. Bot downloads it chunk-by-chunk over WiFi
3. Streams to SD via `M28`, closes with `M29`
4. Bot replies with clickable `▶️ /print FILENAME.GCO`
5. Best for files under 5 MB (115200 baud UART limit)


### 🌐 Web Interface
- Full WebUI control panel
- Embedded maintenance page (terminal, filesystem, firmware updates)
- OTA (Over-the-Air) updates
- Configuration via INI file on SD card
- mDNS auto-discovery (`http://esp3d.local`)

### 💾 Storage & File Management
- SD Card — Native SPI, SDIO (ESP32), SDFat 1.x & 2.x
- Local Filesystem — LittleFS (recommended), FAT (ESP32), SPIFFS
- FTP support — network file transfer
- WebDAV support — remote filesystem access
- Global FS — unified SD + Local FS view under FTP/WebDAV


---

## 🛠️ Supported Hardware

| Platform | Boards |
|----------|--------|
| ESP8266 / ESP8285 | ESP-01, NodeMCU, Wemos D1 Mini |
| ESP32 | Original, Pico, S2, S3, C3, C6 |
| Displays | SSD1306, SH1106 OLED; ST7789 TFT |
| Storage | SD Card (SPI/SDIO), LittleFS, FAT, SPIFFS |

---

## 🚀 Quick Start

### Option A: Arduino IDE (Recommended for ESP32)

1. Open **Arduino IDE**
2. Open: `esp3d/esp3d.ino`
3. Board settings:
   - **Board:** `ESP32 Dev Module`
   - **Partition Scheme:** `Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)`
   - **Upload Speed:** `921600`
   - **Port:** `COM6` (or your ESP32 port)
4. Click **Upload**

### Option B: PlatformIO

```bash
git clone https://github.com/pradeshwaster-design/ESP3D-Enhanced-.git
cd ESP3D-Enhanced-
pip install platformio
pio run
pio run -t upload
pio run -t uploadfs
pio device monitor
```

### First Boot
1. Power on your ESP3D device
2. Connect to the ESP3D Wi-Fi access point
3. Open `http://esp3d.local` in your browser
4. Configure Wi-Fi, printer settings, and preferences via WebUI



---

## 📁 Repository Structure

```
ESP3D-Enhanced/
├── esp3d/              # Main firmware source
│   ├── src/            # Source code
│   │   ├── core/       # Core ESP3D engine
│   │   ├── include/    # Configuration headers
│   │   └── modules/    # Feature modules (WiFi, FTP, WebUI, Telegram, etc.)
│   └── data/           # WebUI files and filesystem assets
├── embedded/           # Embedded web assets
├── libraries/          # External Arduino libraries
├── lua_script_examples/# Example Lua scripts
├── platformIO/         # PlatformIO build scripts
├── tools/              # Utility tools
├── docs/               # Documentation
├── images/             # Repository assets
├── platformio.ini      # PlatformIO project configuration
├── info.json           # Version information
└── README.md           # This file
```

---

---

## 📦 Installation Options

### Using ESP3D-Configurator
Use the [ESP3D-Configurator](https://luc-github.github.io/) web tool to generate your `configuration.h` and download the correct firmware binary for your hardware.

### Manual Build
1. Clone this repository
2. Open in VS Code with PlatformIO extension OR Arduino IDE
3. Select your environment in `platformio.ini` (or configure board in Arduino IDE)
4. Build and upload via PlatformIO or Arduino IDE

---

## 📚 Documentation

Visit **[esp3d.io](http://esp3d.io/esp3d/v3.x/index.html)** for full documentation, installation guides, and API references.

---

## 🐛 Bug Fixes & Known Issues

| Bug | Symptom | Fix |
|-----|---------|-----|
| Double "Hi, esp3d is online" on boot | First poll fetches all old messages, crashes ESP32 | Added `_skipFirstPoll` flag — discards stale messages on first poll |
| `/print` causes crash | Rapid SSL handshakes exhaust heap | Process commands in separate `handle()` cycle from poll |
| Pause triggers false "Print Finished" | Marlin reports "Not SD printing" on M27 during pause | Added `_isPaused` state tracking |
| `/resume` / `/stop` don't work when paused | Marlin's `wait_for_user` loop blocks serial commands | Use `M108` (EMERGENCY_PARSER) instead of `M24`/`M524` alone |
| `/display` shows "Message: 0s" | M117 parser matches stale echoes | Only parse M117 when line starts with "echo:" or "M117" |

### Correct Marlin Serial Commands (Research-Confirmed)

| Action | Correct G-Code | Why |
|--------|---------------|-----|
| Pause | `M25` | Parks nozzle, retracts, enters `wait_for_user` |
| Resume | `M108` | Breaks `wait_for_user` via EMERGENCY_PARSER; Marlin auto-resumes |
| Cancel | `M108` → `M524` → `M104 S0` → `M140 S0` | Breaks wait, aborts SD, cools down |
| Emergency | `M112` | Immediate kill via EMERGENCY_PARSER |

> **Important:** `M24` alone does **NOT** resume when paused! Marlin's blocking `wait_for_user` loop prevents `M24` from being processed. `M108` is the only way to break out via serial.


## 🔧 Configuration

ESP3D is configured through multiple layers:

| Layer | File | Purpose |
|-------|------|---------|
| Compile-time | `esp3d/src/include/esp3d_config.h` | Feature flags, pin assignments, debug level |
| Build | `platformio.ini` | Build environment, upload speed, board, filesystem |
| Runtime | WebUI (`http://esp3d.local`) | Wi-Fi credentials, Telegram token, printer settings |
| Deployment | INI file on SD card | Configuration file for easy deployment |

---

## 🗺️ Upgrade Roadmap

### Tier 1 — Zero Cost, Software Only (Do These Now)
1. **2-Way Telegram Bot** — Interactive control from anywhere (`/status`, `/pause`, `/resume`, `/stop`, `/emergency`, `/temp`, `/cooldown`, `/lights on`)
2. **Power Failure Resume (M413)** — Add `M413 S1` to Cura start G-code
3. **Telegram Thermal Runaway Alert** — Alert if hotend deviates >15°C from target
4. **Tailscale Remote Access** — Access ESP3D dashboard from anywhere (4G/5G), zero port forwarding

### Tier 2 — Low Cost Hardware (Under ₹500 each)
5. **ESP32-CAM Module** (~₹350–500) — Live MJPEG stream, `/photo` command, AI spaghetti detection ready
6. **Filament Runout Sensor** (~₹200–350) — Auto-pause on filament outage, resume via Telegram
7. **Smart Plug / Tasmota Plug** (~₹400–800) — Auto power-off after print completes
8. **DHT22 / BME280 Sensor** (~₹150) — Enclosure temperature & humidity monitoring

### Tier 3 — Medium Investment (~₹800–1200)
9. **ESP32-S3 N16R8 Upgrade** — 16MB Flash + 8MB PSRAM, runs everything standalone on one chip
10. **AI Failure Detection** — TensorFlow Lite on ESP32-S3 + ESP32-CAM, auto-pause on spaghetti detection

### Recommended Order
```
START → 2-Way Telegram Bot → Thermal Runaway Alert → Power Failure Resume → Tailscale
      → Filament Runout Sensor → ESP32-CAM → Smart Plug → DHT22 Sensor
      → ESP32-S3 N16R8 → AI Spaghetti Detection
```

---

## 📄 License

This project is licensed under the **GPL-3.0 License**. See the [LICENSE](LICENSE) file for details.

---

<div align="center">
  <b>ESP3D-Enhanced</b> — Remote control & monitoring for your 3D printer via Telegram & WebUI
</div>
