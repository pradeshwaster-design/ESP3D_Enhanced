# ESP3D-Ender3

[![ESP3D Version](https://img.shields.io/badge/dynamic/json?label=ESP3D&query=$.version&url=https://raw.githubusercontent.com/luc-github/ESP3D/refs/heads/3.0/info.json&color=00bcd4)](https://github.com/luc-github/ESP3D/tree/3.0)
[![License: LGPL v2.1](https://img.shields.io/badge/license-LGPL%20v2.1-blue.svg)](LICENSE)
[![PlatformIO](https://img.shields.io/badge/build-PlatformIO-blueviolet)](https://platformio.org/)
[![ESP32](https://img.shields.io/badge/target-ESP32%2FESP8266%2FESP8285-cyan)](https://www.espressif.com/)
[![3D Printing](https://img.shields.io/badge/application-3D%20printing-orange)](https://github.com/topics/3d-printing)
[![Marlin](https://img.shields.io/badge/printer%20firmware-Marlin-red)](https://marlinfw.org/)

**Wireless connectivity for your 3D printer — ESP3D firmware for ESP32/ESP8266.**

ESP3D-Ender3 is a firmware project focused on enabling convenient wireless connectivity and remote interaction with compatible 3D printers through ESP3D. It runs on ESP32 and ESP8266 modules and bridges your printer's serial connection (e.g. Marlin) to a web interface, telnet, and optional notifications — so you can monitor and control your printer from your local network.

This repository is a community-enhanced fork of the [ESP3D](https://github.com/luc-github/ESP3D) project, built on the v3.0 branch. It includes a custom **Telegram print-status monitor** that periodically reports temperatures, SD-card print progress, and print events to a Telegram chat.

> **Scope note:** Features described here are based on the actual source code in this repository. Some capabilities are inherited from the upstream ESP3D project and are marked accordingly. Where a feature has not been verified in this fork, it is explicitly labeled as unverified.

<details>
<summary>Table of Contents</summary>

- [Overview](#overview)
- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Compatibility Matrix](#compatibility-matrix)
- [Installation](#installation)
- [Configuration](#configuration)
- [Build Instructions](#build-instructions)
- [Firmware Flashing](#firmware-flashing)
- [Usage](#usage)
- [Troubleshooting](#troubleshooting)
- [Security and Safety](#security-and-safety)
- [Contributing](#contributing)
- [License](#license)
- [Credits](#credits)
- [Acknowledgments](#acknowledgments)

</details>

---

## Overview

ESP3D is an open-source firmware for Espressif ESP32 and ESP8266 microcontrollers that adds wireless connectivity to 3D printers, CNC machines, and sand tables. It connects to your printer's mainboard over a serial (UART) link and exposes the printer through:

- A web-based interface (HTTP server)
- Telnet (raw TCP)
- WebSocket
- Optional MQTT, FTP, and WebDAV
- Optional notifications (Telegram, Email, Line, Pushover, IFTTT, Home Assistant)

This fork (`ESP3D-Ender3`) is based on ESP3D v3.0 and adds:

- **Telegram print-status monitor** — parses Marlin serial output (temperatures, SD progress, positions) and sends periodic updates plus print-started/finished notifications to a Telegram chat.
- Simplified PlatformIO configuration targeting a single ESP32 4 MB build environment.

### Architecture

```
┌─────────────────┐     Serial (UART)     ┌─────────────────┐
│   Your Printer  │◄──────────────────────►│    ESP32/       │
│  (Marlin, etc.) │                         │   ESP8266       │
└─────────────────┘      TX/RX/GND        └────────┬────────┘
                                                   │ Wi-Fi / Ethernet
                                                   ▼
                                          ┌─────────────────┐
                                          │  Your Phone /   │
                                          │  PC / Tablet    │
                                          │  (Web / Telnet) │
                                          └─────────────────┘
```
## Features

| Feature | Status | Notes |
|---------|--------|-------|
| Wi-Fi station / AP mode | Verified (source) | `WIFI_FEATURE` in `configuration.h` |
| HTTP web interface | Verified (source) | `HTTP_FEATURE` — built-in web UI for printer control |
| Telnet (raw TCP) | Verified (source) | `TELNET_FEATURE` |
| WebSocket | Verified (source) | WebSocket serial bridge |
| Serial bridge to printer | Verified (source) | UART bridge to Marlin-compatible firmware |
| mDNS / SSDP discovery | Verified (source) | `CAPTIVE_PORTAL_FEATURE`, `MDNS_FEATURE`, `SSDP_FEATURE` |
| Notifications (Telegram, Email, etc.) | Verified (source) | `NOTIFICATION_FEATURE` — multiple backends |
| **Telegram print-status monitor** | **Custom (this fork)** | New `telegram_monitor` module — periodic print status |
| Firmware update via web / OTA / SD | Verified (source) | `WEBUPDATE`, `OTA`, `SDUPDATE` |
| FTP / WebDAV | Verified (source) | Limited FTP (1 connection), WebDAV file access |
| SD card support | Verified (source) | Native SPI, SDIO, SDFat |
| OLED / TFT display | Verified (source) | SSD1306, SH1106, ST7789 |
| Sensors (DHT, BMX280, Analog) | Verified (source) | Temperature/humidity monitoring |
| ESP32 Camera | Verified (source) | Requires PSRAM |
| Lua interpreter | Upstream (work in progress) | Marked WIP in upstream docs |
| Bluetooth serial | Upstream (when BT supported) | Hardware-dependent |
| Ethernet | Verified (source) | `ETH_FEATURE` — LAN8720 support |

### Verified vs. Inherited vs. Planned

- **Verified in this fork:** Source code present and compiled for the `esp32_4MB_wifi` target. Includes the custom `telegram_monitor` module.
- **Inherited from upstream ESP3D v3.0:** Features present in the base ESP3D codebase but not specifically re-tested in this fork.
- **Planned / work in progress:** USB support, Lua interpreter (marked WIP by upstream), some sensor types.

## Hardware Requirements

| Component | Requirement | Notes |
|-----------|-------------|-------|
| Microcontroller | ESP32 (original, Pico, S2, S3, C3, C6), ESP8266, ESP8285 | Build configured for ESP32 Dev Module (4 MB) |
| Flash memory | 4 MB minimum | `board_upload.flash_size = 4MB` |
| Filesystem | LittleFS (recommended) | Configured in `platformio.ini` |
| Printer connection | UART (TX/RX/GND) | GPIO 16 (RX) / GPIO 17 (TX) by default |
| Printer firmware | Marlin 2.x (recommended) | `DEFAULT_FW MARLIN` in `configuration.h` |
| Power | 5 V / 500 mA minimum | USB or external supply |
| USB-to-serial | For flashing and debug | Depends on board (CP2102, CH340, etc.) |
| Antenna | On-board or external | ESP32/ESP8266 Wi-Fi |

**Tested hardware:** ESP32 Dev Module (4 MB) via PlatformIO `esp32dev` environment.  
**Expected to work:** Other ESP32 variants and ESP8266/ESP8285 per upstream ESP3D support, but not specifically verified in this fork.

## Compatibility Matrix

| Component | Supported Version / Model | Status | Notes |
|-----------|---------------------------|--------|-------|
| ESP board | ESP32 Dev Module (4 MB) | Tested | PlatformIO `esp32dev` environment |
| ESP board | ESP8266, ESP8285, other ESP32 variants | Unverified | Supported by upstream ESP3D source |
| Printer firmware | Marlin 2.x | Unverified | `DEFAULT_FW MARLIN` configured |
| Printer model | Ender 3 (and compatible) | Unverified | Repository name suggests Ender 3 target |
| Build toolchain | PlatformIO Core | Tested | `platformio.ini` at repository root |
| Web UI | ESP3D-WEBUI v3.0 | Unverified | Separate upstream repository |
| Notifications | Telegram (custom module) | Verified (source) | New `telegram_monitor` module |

## Installation

### 1. Download or Clone the Repository

```bash
git clone https://github.com/pradeshwaster-design/ESP3D-Enhanced.git
cd ESP3D-Enhanced
```

> **Note:** Replace the URL with your actual fork URL if different.

### 2. Install Development Tools

Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/install.html) (recommended) or use [PlatformIO IDE for VS Code](https://platformio.org/install/ide?install=vscode).

PlatformIO is required because this project uses a `platformio.ini` build configuration.

### 3. Select the Correct Target Board

The default environment in `platformio.ini` is:

```
default_envs = esp32_4MB_wifi
```

This targets the **ESP32 Dev Module** with 4 MB flash. To build for a different board, add or modify environments in `platformio.ini` and select the desired environment with:

```bash
platformio run -e <environment_name>
```

### 4. Install Dependencies

PlatformIO resolves library dependencies automatically from `platformio.ini` and the `libraries/` / `extra-libraries/` folders. No manual dependency installation is required when using PlatformIO.

### 5. Configure the Firmware

Edit `esp3d/configuration.h` to match your hardware and network. See the [Configuration](#configuration) section below.

**Never commit real Wi-Fi passwords or credentials.** Use placeholders:

```c
// #define STATION_WIFI_SSID "YOUR_SSID"
// #define STATION_WIFI_PASSWORD "YOUR_PASSWORD"
```

Alternatively, create a `myconfig.h` file alongside `configuration.h` (it is git-ignored) with your actual credentials.

### 6. Build the Firmware

```bash
platformio run
```

The compiled firmware binary and filesystem image are placed in `.pioenvs/esp32_4MB_wifi/`.

### 7. Connect the Board

Connect your ESP32/ESP8266 to your computer via USB. Identify the correct serial port (e.g., `COM6` on Windows, `/dev/ttyUSB0` on Linux).

### 8. Flash the Firmware

```bash
platformio run -e esp32_4MB_wifi -t upload
```

Make sure `upload_port` in `platformio.ini` matches your board's serial port.

### 9. Configure Wireless Networking

After flashing, the ESP will attempt to connect to your configured Wi-Fi network. If credentials are not set or connection fails, the device falls back to AP mode with a captive portal for initial setup.

### 10. Connect to the Printer

Wire the ESP UART to your printer's mainboard serial port:
- ESP TX → Printer RX
- ESP RX → Printer TX
- ESP GND → Printer GND

Ensure the baud rate in `configuration.h` matches your printer firmware (default 115200 for Marlin).

### 11. Verify Successful Operation

- Open the web interface at `http://<esp-ip-address>` (or use mDNS: `http://esp3d.local`)
- Check the serial monitor at 115200 baud for boot messages
- Confirm the printer responds to commands via the web terminal

## Configuration

Important options in `esp3d/configuration.h`:

### Wi-Fi / Network

```c
// Station mode (connects to your router)
// #define STATION_WIFI_SSID "YOUR_SSID"
// #define STATION_WIFI_PASSWORD "YOUR_PASSWORD"

// Access Point fallback
#define WIFI_FEATURE
#define CAPTIVE_PORTAL_FEATURE
#define MDNS_FEATURE
#define SSDP_FEATURE
```

### Serial / Printer Connection

```c
#define COMMUNICATION_PROTOCOL RAW_SERIAL
#define ESP_SERIAL_OUTPUT USE_SERIAL_2
#define ESP_RX_PIN 16
#define ESP_TX_PIN 17
#define SERIAL_RX_BUFFER_SIZE 512
#define DEFAULT_FW MARLIN
```

### Enabled Channels

```c
#define HTTP_FEATURE      // Web interface
#define TELNET_FEATURE    // Raw TCP terminal
#define NOTIFICATION_FEATURE  // Telegram, email, etc.
```

### Custom: Telegram Print Monitor (this fork)

Enabled when `NOTIFICATION_FEATURE` is defined. Configure your Telegram bot token and chat ID through the ESP3D web interface or notification settings. The monitor parses Marlin auto-report data and sends periodic status updates.

### Persistent Configuration

Settings can be stored in EEPROM (`SETTINGS_IN_EEPROM`) or overridden via a `myconfig.h` file placed next to `configuration.h` (git-ignored).

### Reset and Recovery

- `RESET_FEATURE` / `RESET_PIN` — hardware reset trigger
- Recovery pin support for safe mode entry

## Build Instructions

### Required Software

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/install.html) or PlatformIO IDE (VS Code)
- Python 3.x (for PlatformIO)
- Git

### Build Commands

```bash
# Build default environment (ESP32 Dev Module, 4 MB)
platformio run

# Build and upload
platformio run -e esp32_4MB_wifi -t upload

# Clean build
platformio run --target clean
platformio run

# Monitor serial output
platformio device monitor -p COM6 -b 115200
```

### Expected Output Files

| File | Location | Description |
|------|----------|-------------|
| Firmware binary | `.pioenvs/esp32_4MB_wifi/firmware.bin` | Main firmware image |
| Filesystem image | `.pioenvs/esp32_4MB_wifi/littlefs.bin` | LittleFS filesystem (web UI files) |

### Custom Build Scripts

The project uses `platformIO/extra_script.py` as a pre-build step. Review this script if you need to customize the build process.

### Common Build Issues

| Issue | Cause | Fix |
|-------|-------|-----|
| Compilation errors | Missing libraries | PlatformIO auto-installs dependencies; ensure internet access |
| `upload_port` not found | Wrong COM port | Update `upload_port` in `platformio.ini` |
| Flash size mismatch | Board has different flash | Adjust `board_upload.flash_size` and partitions |

## Firmware Flashing

### Using PlatformIO (Recommended)

1. Connect the ESP board via USB.
2. Verify `upload_port` in `platformio.ini` (default: `COM6`).
3. Run:

```bash
platformio run -e esp32_4MB_wifi -t upload
```

### Manual Flashing

If you need to flash manually (e.g., using `esptool.py`):

1. Put the ESP into bootloader mode (hold GPIO0 to GND, reset, release GPIO0).
2. Identify the correct flash address and binary from `.pioenvs/esp32_4MB_wifi/`.
3. Flash using `esptool.py` or your preferred tool.

### Bootloader / Download Mode

- **ESP32:** Hold GPIO0 to GND, press Reset, release GPIO0.
- **ESP8266:** Hold GPIO0 to GND, power cycle or reset, release GPIO0.

### Recovery from Flash Errors

- If the board does not boot, re-enter bootloader mode and re-flash.
- Ensure the correct flash mode (DIO/QIO) and flash size are set.
- Check `upload_speed` — lower to 460800 if you see header packet errors.

## Usage

After flashing and connecting:

1. **Power on** the ESP and printer.
2. **Connect to Wi-Fi** — the ESP will join your network or create an AP.
3. **Open the web interface** — `http://<esp-ip-address>` or `http://esp3d.local`
4. **Connect to the printer** — use the web terminal or send G-code commands.
5. **Monitor print status** — view temperatures, SD progress, and positions.
6. **Telegram notifications** (this fork) — receive periodic print updates if configured.

### Web Interface

The built-in HTTP server provides:
- Printer control and monitoring
- G-code terminal
- File management (SD card)
- Firmware update
- Configuration

### Serial Commands

ESP3D supports ESPxxx command codes for configuration and control. See `docs/espXXX.md` for the full command reference.

## Troubleshooting

| Problem | Likely Cause | Solution |
|---------|--------------|----------|
| Board not detected | Wrong cable, missing drivers | Try a different USB cable; install CP2102/CH340 drivers |
| Compilation failure | Missing dependencies, wrong board | Run `platformio run`; check `platformio.ini` environment |
| Flashing error | Wrong port, wrong boot mode | Verify `upload_port`; enter bootloader mode |
| Device not booting | Bad flash, wrong partition table | Re-flash; verify `board_build.partitions` |
| Wi-Fi not connecting | Wrong credentials, weak signal | Check SSID/password; move closer to router |
| Web interface unreachable | Wrong IP, firewall, AP mode | Check serial monitor for IP; try `http://esp3d.local` |
| Printer not responding | Wrong baud rate, wiring | Verify baud rate (default 115200); check TX/RX/GND wiring |
| No Telegram notifications | Bot token/chat ID missing | Configure notification settings; verify `NOTIFICATION_FEATURE` |

> General diagnostic suggestions marked without a hardware test badge have not been verified on the target hardware in this fork.

## Security and Safety

- **Never commit real Wi-Fi credentials.** Use `myconfig.h` (git-ignored) or placeholders in `configuration.h`.
- **Do not expose the printer-control web interface directly to the public internet.** Use it only on your local network or behind a VPN.
- **Authentication is optional.** When enabled, it provides guest/user/admin levels but passwords are stored in clear text. Do not rely on it for public-facing security.
- **Keep firmware and dependencies updated.** Review the upstream ESP3D repository for security patches.
- **Secure firmware update mechanisms.** Only install firmware from trusted sources.
- **Review configuration before sharing.** Remove passwords, tokens, and IP addresses from config files before publishing.
- **Printer safety:** Disconnect or secure the printer before testing potentially dangerous motion commands. Wireless control can move axes unexpectedly.

This project does not claim HTTPS, encrypted password storage, or advanced access control beyond what is documented in the source.

## Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for:

- Setting up the development environment
- Building and testing changes
- Code formatting expectations
- How to report bugs and submit pull requests

All contributors must follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## License

This project is licensed under the [GNU Lesser General Public License v2.1](LICENSE) (LGPL-2.1), the same license as the upstream ESP3D project.

## Credits

### Original Project

- **ESP3D** by Luc Lebosse — https://github.com/luc-github/ESP3D
- This repository is a community-enhanced fork based on ESP3D v3.0.

### Libraries and Dependencies

- [Arduino-Ftp-Server](https://github.com/gallegojm/Arduino-Ftp-Server) — FTP server
- [ESPWebDAV](https://github.com/d-a-v/ESPWebDAV) — WebDAV server
- [arduinoWebSockets](https://github.com/Links2004/arduinoWebSockets) — WebSocket client/server
- [SdFat](https://github.com/greiman/SdFat) — SD card library
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) — TFT display driver
- And others — see `docs/` and source headers for full attribution.

### Contributors

See the contributors section in the original ESP3D repository and the `.all-contributorsrc` file.

## Acknowledgments

- The ESP3D open-source community
- Espressif Systems for ESP32/ESP8266 chips
- Marlin firmware developers
- All library authors listed above

---

*Last updated: October 2026 — This README reflects the actual source code in this repository. Unverified claims are explicitly marked.*
