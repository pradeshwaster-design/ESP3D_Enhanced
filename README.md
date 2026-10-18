# 🚀 ESP3D Enhanced — Standalone Wireless 3D Print Server & Telegram Bot

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-red.svg)](https://www.espressif.com/)
[![Firmware: Marlin 2.x](https://img.shields.io/badge/Marlin-2.x%20Ready-brightgreen.svg)](https://marlinfw.org/)
[![Telegram Bot API](https://img.shields.io/badge/Telegram-Bot%20API%20Enabled-blue.svg?logo=telegram)](https://core.telegram.org/bots/api)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange.svg?logo=platformio)](https://platformio.org/)

**ESP3D Enhanced** transforms an inexpensive ESP32 microcontroller into an enterprise-grade, standalone wireless 3D print server and tele-control hub for Marlin-based 3D printers.

Unlike traditional setups that require a power-hungry Raspberry Pi running OctoPrint/Klipper or insecure port forwarding, **ESP3D Enhanced** connects directly to your printer's mainboard UART and interfaces natively with the **Telegram Cloud Bot API**. Control, monitor, tune, and upload files to your 3D printer securely from anywhere in the world — directly from your phone, tablet, or desktop.

---

## 🌟 Key Features

- 🤖 **Interactive 2-Way Telegram Bot**: Full bi-directional command center (`/status`, `/temp`, `/pause`, `/resume`, `/stop`, `/info`).
- 📁 **Direct SD Card G-code Upload (Up to 20 MB)**:
  - Simply drop a `.gcode` file into your private Telegram chat.
  - Automatically handles **HTTP 302/307 CDN redirects** from Telegram cloud servers.
  - Slicer comments and whitespace are stripped on the fly to maximize transmission speeds.
  - Lines are validated with Marlin XOR checksums (`N<line> G1 ... *<cs>`).
  - Saved directly to the printer SD card via high-speed serial streaming (`M28`/`M29`).
- 🗂️ **SD Card File Management**:
  - `/files`: Lists files stored on the printer's SD card.
  - Single-tap print commands: `/print_1`, `/print_2`, etc.
  - Single-tap remote file deletion: `/del_1`, `/del_2` (via `M30`).
- 🎛️ **Real-Time Print Tuning**:
  - `/speed <percentage>`: Mid-print feedrate override (e.g. `/speed 120` for 120%).
  - `/flow <percentage>`: Mid-print extrusion flow override (e.g. `/flow 95` for 95%).
  - `/fan <0-255 | %>`: Real-time part cooling fan speed control.
- 📏 **Live Z-Offset Babystepping**:
  - `/zoffset <value>`: Fine-tune probe Z-offset on the fly during first-layer printing (M290 babystepping).
- 🛡️ **Marlin Host Prompt & ADVANCED_PAUSE Auto-Break**:
  - Solves the infamous Marlin 2.x `wait_for_user` freeze during pause and filament change routines.
  - Automatically acknowledges host prompts (`//action:prompt_begin`, `//action:prompt_button Continue`) using `M876 S1` and emergency parser `M108`.
- 🛑 **Emergency Halt & Communication Lock**:
  - `/emergency`: Instantly triggers Marlin emergency stop (`M112`), cuts heater/motor power, and locks serial communication until `/reboot`.
- 🌡️ **One-Tap Preheat Macros**:
  - `/preheat_pla`: Heats Hotend to 200°C and Bed to 60°C.
  - `/preheat_petg`: Heats Hotend to 240°C and Bed to 75°C.
  - `/cooldown`: Shuts down all heaters (`M104 S0`, `M140 S0`).
- 🧵 **Automated Filament Macros**:
  - `/load`: Extrudes filament with built-in cold extrusion protection (nozzle heated to 200°C before extrusion).
  - `/unload`: Retracts filament cleanly out of the hotend.
- 🔒 **Bulletproof Hardware Serial Mutex Lock**:
  - Zero packet collisions: Strict ping-pong `ok` synchronization ensures serial data from the web UI and SD uploader never overlap or cause checksum resend errors.

---

## 🏛️ System Architecture

```text
  ┌─────────────────────────────────────────────────────────┐
  │                 Telegram Cloud API                      │
  │     (Encrypted Mobile / Desktop App Interface)          │
  └──────────────────────────┬──────────────────────────────┘
                             │ HTTPS / TLS (Poll & Push)
                             ▼
  ┌─────────────────────────────────────────────────────────┐
  │              ESP32 (ESP3D Enhanced Core)                │
  │                                                         │
  │  ┌───────────────────────┐   ┌───────────────────────┐  │
  │  │ Telegram Monitor Core │   │   Web Telemetry UI    │  │
  │  │ - 2-Way Command Engine│   │   - Dashboard / Term  │  │
  │  │ - 20 MB CDN Downloader│   │   - Configuration     │  │
  │  │ - G-Code Line Parser  │   │   - OTA Flasher       │  │
  │  └───────────┬───────────┘   └───────────┬───────────┘  │
  │              │                           │              │
  │              └─────────────┬─────────────┘              │
  │                            ▼                            │
  │            [ Thread-Safe Serial Mutex Lock ]            │
  │                            │                            │
  │                 Hardware UART2 (Serial)                 │
  └────────────────────────────┬────────────────────────────┘
                               │ TX2 (GPIO 17) / RX2 (GPIO 16)
                               ▼
  ┌─────────────────────────────────────────────────────────┐
  │             3D Printer Controller Board                 │
  │      (Creality 4.2.2/4.2.7, BTT SKR, MKS Robin, etc.)    │
  │                                                         │
  │  Marlin 2.x Firmware:                                   │
  │  - SD Card Controller (M20, M28, M29, M30)              │
  │  - Emergency Parser (M108, M112, M876)                  │
  │  - Live Babystepping (M290)                             │
  │  - Motion & Thermal Management                          │
  └─────────────────────────────────────────────────────────┘
```

---

## 📱 Telegram Bot Command Reference

| Command | Arguments | Description | G-code Mapped |
|---|---|---|---|
| `/status` | — | Queries current printer status, temperatures, fan speed, active file | `M105` |
| `/temp` | — | Detailed hotend and heated bed telemetry | `M105` |
| `/info` | — | Diagnostics: ESP32 uptime, free heap, Wi-Fi RSSI, IP, build info | — |
| `/files` | — | Interactive list of SD card files with one-tap print/delete links | `M20` |
| `/print_<n>` | `<n>` | Starts printing file number `<n>` from the `/files` list | `M23` + `M24` |
| `/print` | `<filename>` | Starts printing specified file directly | `M23` + `M24` |
| `/pause` | — | Pauses active print safely | `M25` |
| `/resume` | — | Resumes paused print | `M24` |
| `/stop` | — | Aborts active print and turns off heaters | `M876 S1` + `M108` + `M524` + `M104 S0` + `M140 S0` |
| `/del_<n>` | `<n>` | Deletes file number `<n>` from the printer SD card | `M30` |
| `/del` | `<filename>` | Deletes specified file from the printer SD card | `M30` |
| `/speed` | `<percent>` | Adjusts print speed override (e.g. `/speed 110`) | `M220 S<val>` |
| `/flow` | `<percent>` | Adjusts extrusion flow override (e.g. `/flow 98`) | `M221 S<val>` |
| `/fan` | `<0-255 \| %>` | Sets part cooling fan speed (e.g. `/fan 100%` or `/fan 255`) | `M106 S<val>` |
| `/zoffset` | `<mm>` | Live babystepping of probe Z-offset (e.g. `/zoffset -0.05`) | `M290 Z<val>` |
| `/preheat_pla` | — | Sets hotend to 200°C and bed to 60°C | `M104 S200` + `M140 S60` |
| `/preheat_petg`| — | Sets hotend to 240°C and bed to 75°C | `M104 S240` + `M140 S75` |
| `/cooldown` | — | Shuts down hotend and bed heaters immediately | `M104 S0` + `M140 S0` |
| `/load` | — | Heats nozzle to 200°C and extrudes filament safely | Macro |
| `/unload` | — | Heats nozzle to 200°C and retracts filament | Macro |
| `/emergency` | — | **EMERGENCY STOP**: Halts motion, kills heaters, locks serial | `M112` |
| `/reboot` | — | Performs software restart of the ESP32 module | Hardware Reset |

---

## 🔌 Hardware Wiring & Pinout

Connect the ESP32 development board (e.g. ESP32 NodeMCU / DevKit V1) to your printer motherboard's serial/TFT/AUX port:

| ESP32 Pin | Printer Motherboard (UART Header) | Description |
|---|---|---|
| **GPIO 16 (RX2)** | **TX** | Serial Data from Motherboard to ESP32 |
| **GPIO 17 (TX2)** | **RX** | Serial Data from ESP32 to Motherboard |
| **GND** | **GND** | Common Ground |
| **VIN / 5V** | **5V** | Regulated 5V Power Supply |

> [!IMPORTANT]
> - Ensure common **GND** is firmly connected between the ESP32 and the printer motherboard.
> - Default baud rate: **115200** or **250000** (must match Marlin `BAUDRATE`).

---

## 🛠️ Installation & Setup Guide

### 1. Create Your Telegram Bot
1. Open Telegram and search for [`@BotFather`](https://t.me/BotFather).
2. Send `/newbot` and follow the prompts to choose a name and username.
3. Save the **HTTP API Token** provided (format: `123456789:ABCdefGhIJKlmNoPQRsTUVwxyZ`).
4. Find your Telegram numeric User ID by messaging [`@userinfobot`](https://t.me/userinfobot) (e.g. `987654321`).

### 2. Compile & Flash
Build using **PlatformIO** (VS Code extension or CLI):

```bash
# Clone the repository
git clone https://github.com/pradeshwaster-design/ESP3D_Enhanced.git
cd ESP3D_Enhanced

# Build firmware for standard 4MB ESP32
pio run -e esp32_4MB_wifi

# Flash over USB
pio run -e esp32_4MB_wifi -t upload
```

### 3. Over-The-Air (OTA) Updates
Once flashed, all future firmware upgrades can be performed over Wi-Fi without connecting a USB cable:
1. Navigate to `http://<esp32-ip>/update` in your browser.
2. Upload `.pioenvs/esp32_4MB_wifi/firmware.bin`.
3. The ESP32 will verify the image, flash itself, and reboot automatically.

---

## 📋 Version History & Changelog

- **v1.18.0 (V19)**: HTTP Range auto-resume engine (`Range: bytes=start-`) and heap-isolated streaming for multi-chunk uploads up to 20 MB.
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

## 📄 License & Credits

- Based on the open-source [ESP3D](https://github.com/luc-github/ESP3D) project by Luc Lebosse (GPL-3.0 License).
- **ESP3D Enhanced** extensions, 2-way Telegram bot engine, serial mutex uploader, and Marlin prompt automation created by **[pradeshwaster-design](https://github.com/pradeshwaster-design)**.
- Licensed under the [GNU General Public License v3.0](LICENSE).
