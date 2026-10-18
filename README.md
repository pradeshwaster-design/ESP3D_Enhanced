<div align="center">

<img src="assets/banner.svg" alt="ESP3D Enhanced Banner" width="100%">

# 🚀 ESP3D Enhanced
### Standalone Wireless 3D Print Server & 2-Way Telegram Bot for Marlin

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-red.svg?style=flat-square&logo=espressif)](https://www.espressif.com/)
[![Firmware: Marlin 2.x](https://img.shields.io/badge/Marlin-2.x%20Ready-brightgreen.svg?style=flat-square)](https://marlinfw.org/)
[![Telegram Bot API](https://img.shields.io/badge/Telegram-Bot%20API-blue.svg?style=flat-square&logo=telegram)](https://core.telegram.org/bots/api)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange.svg?style=flat-square&logo=platformio)](https://platformio.org/)
[![C++](https://img.shields.io/badge/Language-C++17-00599C.svg?style=flat-square&logo=c%2B%2B)](https://isocpp.org/)

<p align="center">
  <b>Transform an inexpensive ESP32 microcontroller into a complete, standalone wireless 3D print server and bi-directional tele-control hub for Marlin 3D printers.</b><br>
  No Raspberry Pi • No OctoPrint required • No port forwarding • Secure Telegram Cloud encryption
</p>

[Key Features](#-key-features) • [System Architecture](#-system-architecture) • [Command Reference](#-telegram-bot-command-reference) • [Hardware & Wiring](#-hardware-wiring--pinout) • [Flashing Guide](#-installation--flashing-guide) • [Changelog](#-version-history--changelog)

</div>

---

> [!NOTE]
> **Recommended GitHub Repository Description & Tags:**
> - **Description**: `Autonomous IoT wireless 3D print server & 2-way Telegram bot for Marlin with 20MB over-the-air SD G-code streaming, live tuning, and emergency fail-safes.`
> - **Tags / Topics**: `esp32`, `marlin-firmware`, `3d-printing`, `telegram-bot`, `esp3d`, `gcode`, `iot`, `print-server`, `remote-monitoring`, `octoprint-alternative`

---

## 🌟 Key Features

| Category | Capability | Description |
|---|---|---|
| 🔘 **Touch UI** | **Inline & Reply Keyboards** | One-tap interactive buttons directly attached to status reports (`[⏸️ Pause] [⏹️ Stop] [📊 Status] [📁 Files]`) + persistent bottom keyboard. |
| ⚡ **OTA Flash** | **In-Chat Wireless Firmware Update** | Drag and drop any `firmware.bin` into Telegram chat to flash the ESP32 wirelessly over the air without USB or Arduino IDE. |
| 🎯 **Smart Alerts**| **Layer 1 & Milestone Tracker** | Automated proactive alerts on Layer 1 completion (bed adhesion check), 25%, 50%, 75% progress milestones, and safe-to-touch bed cooldown (≤ 42°C). |
| 🤖 **2-Way Bot** | **Interactive Telegram Control** | Full bi-directional command center: query status, temperatures, pause, resume, abort, and tune parameters from anywhere in the world. |
| ⚡ **SD Upload** | **20 MB Cloud G-Code Streaming** | Send `.gcode` files directly via Telegram chat. Handles HTTP 302/307 redirects, HTTP Range resuming (`Range: bytes=start-`), comment stripping, and Marlin XOR checksums. |
| 🗂️ **SD Manager** | **Interactive File Browser** | `/files` lists SD files with instant one-tap print (`/print_1`) and delete (`/del_1`) command links. |
| 🎛️ **Live Tuning** | **Mid-Print Overrides** | Dynamically adjust feedrate speed (`/speed`), extrusion flow (`/flow`), and cooling fan (`/fan`) on the fly without touching the printer LCD. |
| 📏 **Babystepping** | **Probe Z-Offset Adjustment** | Fine-tune nozzle first-layer squish directly from your phone using `/zoffset <mm>` (`M290`). |
| 🛡️ **Pause Auto-Break** | **Marlin Prompt Resolution** | Automatically skips blocking `wait_for_user` loops and resolves PurgeMore/Continue host prompts (`M876 S1` + `M108`). |
| 🛑 **Safety Fail-Safe** | **Emergency Halt & Port Lock** | `/emergency` immediately fires `M112`, cuts power to heaters and steppers, and locks the serial port until a manual `/reboot`. |
| 🌡️ **Macros** | **Preheating & Filament Handling** | One-tap PLA (`/preheat_pla`), PETG (`/preheat_petg`), cooldown (`/cooldown`), and cold-extrusion-protected `/load` & `/unload`. |
| 🔒 **Serial Mutex** | **Zero Packet Collisions** | Hardware serial mutex lock with strict ping-pong `ok` synchronization eliminates resend loops and buffer overflow hangs. |

---

## 🏛️ System Architecture

```text
 ┌─────────────────────────────────────────────────────────────┐
 │                     Telegram Cloud API                      │
 │         (Encrypted Mobile & Desktop App Telemetry)          │
 └──────────────────────────────┬──────────────────────────────┘
                                │ HTTPS / TLS (Poll & Push)
                                ▼
 ┌─────────────────────────────────────────────────────────────┐
 │                ESP32 (ESP3D Enhanced Core)                  │
 │                                                             │
 │   ┌───────────────────────────┐ ┌─────────────────────────┐ │
 │   │   Telegram Monitor Core   │ │    Web Telemetry UI     │ │
 │   │   - 2-Way Command Engine  │ │    - Real-Time Monitor  │ │
 │   │   - 20 MB CDN Downloader  │ │    - G-Code Terminal    │ │
 │   │   - HTTP Range Auto-Resume│ │    - Settings & Config  │ │
 │   │   - Slicer Comment Stripper│ │                        │ │
 │   └─────────────┬─────────────┘ └────────────┬────────────┘ │
 │                 │                            │              │
 │                 └──────────────┬─────────────┘              │
 │                                ▼                            │
 │               [ Hardware Serial Mutex Lock ]                │
 │                                │                            │
 │                     Hardware UART2 (Serial)                 │
 └──────────────────────────────┬──────────────────────────────┘
                                │ TX2 (GPIO 17) / RX2 (GPIO 16)
                                ▼
 ┌─────────────────────────────────────────────────────────────┐
 │                3D Printer Controller Board                  │
 │         (Creality 4.2.2/4.2.7, BTT SKR, MKS, etc.)          │
 │                                                             │
 │   Marlin 2.x Firmware:                                      │
 │   - SD Card Controller (M20, M28, M29, M30)                 │
 │   - Emergency Parser (M108, M112, M876)                     │
 │   - Live Babystepping (M290)                                │
 │   - Thermal & Motion Executive                              │
 └─────────────────────────────────────────────────────────────┘
```

---

## 📱 Telegram Bot Command Reference

### 📊 Monitoring & Telemetry
| Command | Arguments | Description | Marlin G-code |
|---|---|---|---|
| `/status` | — | Queries current printer state, temperatures, fan, and progress | `M105` |
| `/temp` | — | Detailed hotend and heated bed telemetry | `M105` |
| `/info` | — | Diagnostics: ESP32 uptime, free heap, Wi-Fi RSSI, IP, build version | — |
| `/files` | — | Interactive list of SD card files with one-tap print/delete links | `M20` |

### 🖨️ Print Control
| Command | Arguments | Description | Marlin G-code |
|---|---|---|---|
| `/print_<n>` | `<n>` | Starts printing file number `<n>` from the `/files` list | `M23` + `M24` |
| `/print` | `<filename>` | Starts printing the specified file directly | `M23` + `M24` |
| `/pause` | — | Safely pauses the active print job | `M25` |
| `/resume` | — | Resumes paused print job | `M24` |
| `/stop` | — | Aborts print and turns off heaters | `M876 S1` + `M108` + `M524` + `M104 S0` + `M140 S0` |

### 🗑️ File Management
| Command | Arguments | Description | Marlin G-code |
|---|---|---|---|
| `/del_<n>` | `<n>` | Deletes file number `<n>` from the printer SD card | `M30` |
| `/del` | `<filename>` | Deletes specified filename from the SD card | `M30` |

### 🎛️ Live Tuning & Micro-Stepping
| Command | Arguments | Description | Marlin G-code |
|---|---|---|---|
| `/speed` | `<percent>` | Adjusts print speed override (e.g. `/speed 115`) | `M220 S<val>` |
| `/flow` | `<percent>` | Adjusts extrusion flow override (e.g. `/flow 98`) | `M221 S<val>` |
| `/fan` | `<0-255 \| %>`| Adjusts cooling fan speed (e.g. `/fan 100%` or `/fan 255`) | `M106 S<val>` |
| `/zoffset` | `<mm>` | Live babystepping of probe Z-offset (e.g. `/zoffset -0.05`) | `M290 Z<val>` |

### 🌡️ Temperature & Filament Macros
| Command | Arguments | Description | Marlin G-code |
|---|---|---|---|
| `/preheat_pla` | — | Sets hotend to 200°C and bed to 60°C | `M104 S200` + `M140 S60` |
| `/preheat_petg`| — | Sets hotend to 240°C and bed to 75°C | `M104 S240` + `M140 S75` |
| `/cooldown` | — | Shuts down hotend and bed heaters immediately | `M104 S0` + `M140 S0` |
| `/load` | — | Heats nozzle to 200°C and extrudes filament safely | Automated Macro |
| `/unload` | — | Heats nozzle to 200°C and retracts filament | Automated Macro |

### 🛑 Emergency & Maintenance
| Command | Arguments | Description | Marlin G-code |
|---|---|---|---|
| `/emergency` | — | **EMERGENCY STOP**: Halts motion, kills heaters, locks serial | `M112` |
| `/reboot` | — | Reconnects and verifies Marlin over serial | `M999` + `M105` |
| `/reboot_esp` | — | Performs hardware restart of the ESP32 microcontroller | Software Reset |
| `/ota` | — | Displays instructions for in-chat Over-The-Air flashing | — |
| `/start` | — | Displays welcome menu and summons persistent quick-action keyboard | — |

---

## 🔌 Hardware Wiring & Pinout

Connect the ESP32 development board (e.g. ESP32 NodeMCU / DevKit V1) to your printer motherboard's serial/TFT/AUX port:

```text
  ESP32 Dev Board                 3D Printer Mainboard (UART Header)
 ┌───────────────┐               ┌──────────────────────────────────┐
 │ GPIO 16 (RX2) │ <───────────  │ TX  (Serial Out from Motherboard)│
 │ GPIO 17 (TX2) │ ───────────>  │ RX  (Serial In to Motherboard)   │
 │ GND           │ <───────────> │ GND (Common Ground)              │
 │ VIN / 5V      │ <───────────  │ 5V  (Regulated Power Supply)     │
 └───────────────┘               └──────────────────────────────────┘
```

> [!IMPORTANT]
> 1. **Common Ground**: Always ensure the GND pin of the ESP32 is firmly connected to the motherboard's GND pin to prevent floating logic levels and serial packet corruption.
> 2. **Baud Rate**: The ESP3D serial baud rate must match the `BAUDRATE` setting in your Marlin `Configuration.h` (default: **115200** or **250000**).

---

## 🛠️ Installation & Flashing Guide

### Method 1: Flashing via PlatformIO (Recommended)

1. Clone the repository:
   ```bash
   git clone https://github.com/pradeshwaster-design/ESP3D_Enhanced.git
   cd ESP3D_Enhanced
   ```
2. Build the firmware:
   ```bash
   pio run -e esp32_4MB_wifi
   ```
3. Connect your ESP32 via USB and flash:
   ```bash
   pio run -e esp32_4MB_wifi -t upload
   ```

### Method 2: Flashing via Arduino IDE

1. Open Arduino IDE and add ESP32 Board Support:
   - **Board URL**: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Open `esp3d/esp3d.ino`.
3. Select board settings:
   - **Board**: `ESP32 Dev Module`
   - **Flash Size**: `4MB (32Mb)`
   - **Partition Scheme**: `Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)` or `Minimal SPIFFS (1.9MB APP / 190KB SPIFFS)`
   - **Upload Speed**: `921600` or `115200`
4. Click **Upload** (hold the `BOOT` button on your ESP32 if upload does not start automatically).

### Method 3: Direct Binary Flash via esptool.py

If you already have the precompiled `firmware.bin`:
```bash
python -m esptool --chip esp32 --baud 921600 write_flash 0x10000 firmware.bin
```

---

## ⚙️ Initial Configuration

1. **Create your Telegram Bot**:
   - Message [`@BotFather`](https://t.me/BotFather) on Telegram and run `/newbot`.
   - Copy your **HTTP API Bot Token** (`123456789:ABCdef...`).
2. **Retrieve your Chat ID**:
   - Message [`@userinfobot`](https://t.me/userinfobot) to get your numeric Telegram user ID (`987654321`).
3. **Configure in ESP3D Web UI**:
   - Connect to ESP3D Wi-Fi AP (`ESP3D` / default password: `admin`).
   - Navigate to **Settings** $\to$ **Notifications**.
   - Select **Telegram**, enter your Bot Token and Chat ID, and click **Save & Restart**.

---

## 📋 Version History & Changelog

- **v1.18.0 (V19)**: HTTP Range auto-resume engine (`Range: bytes=start-`) with HTTP 206 Partial Content support and TLS heap isolation, enabling multi-chunk uploads up to 20 MB.
- **v1.17.0 (V18)**: Telegram Cloud 20 MB CDN redirect engine (HTTP 302/307) and 35s upload timeout.
- **v1.16.0 (V17)**: Bulletproof hardware serial mutex lock and strict ping-pong `ok` synchronization.
- **v1.15.0 (V16)**: Remote SD file deletion (`/del`, `/del_<n>`) and dynamic upload duration estimator.
- **v1.14.0 (V15)**: Serial transmission pacing and buffer overflow protection.
- **v1.13.0 (V14)**: Interactive SD card file browser (`/files`) with single-tap `/print_<n>` links.
- **v1.12.0 (V13)**: Layer height and total layer count tracking (`;LAYER`, `;LAYER_COUNT`).
- **v1.11.0 (V12)**: Slicer comment stripping, whitespace compaction, and line-by-line checksum generation.
- **v1.10.0 (V11)**: Non-blocking Serial M28/M29 G-code SD card uploader state machine.
- **v1.9.0 (V10)**: Live probe Z-offset babystepping (`/zoffset`) and `/info` system diagnostics.
- **v1.8.0 (V9)**: Real-time print tuning controls for speed (`/speed`), flow (`/flow`), and fan (`/fan`).
- **v1.7.0 (V8)**: Stop cooldown guard suppressing trailing Marlin SD progress reports.
- **v1.6.0 (V7)**: Preheat presets (`/preheat_pla`, `/preheat_petg`, `/cooldown`).
- **v1.5.0 (V6)**: Dynamic prompt button indexing and purge-more auto-skipping.
- **v1.4.0 (V5)**: Marlin host prompt auto-response engine (`M876 S1` / `M108`).
- **v1.3.0 (V4)**: Automated filament load and unload macros (`/load`, `/unload`) with cold extrusion guard.
- **v1.2.0 (V3)**: Marlin `ADVANCED_PAUSE_FEATURE` `wait_for_user` auto-break and `/emergency` halt.
- **v1.1.0 (V2)**: Interactive 2-way Telegram bot commands listener and authentication parser.
- **v1.0.0 (V1)**: Initial ESP3D baseline with custom Telegram notification monitor.

---

## 📄 License & Acknowledgements

- Based on the open-source [ESP3D 3.0](https://github.com/luc-github/ESP3D) project by Luc Lebosse (GPL-3.0 License).
- **ESP3D Enhanced** extensions, 2-way Telegram bot engine, serial mutex uploader, and Marlin prompt automation created and maintained by **[pradeshwaster-design](https://github.com/pradeshwaster-design)**.
- Licensed under the [GNU General Public License v3.0](LICENSE).
