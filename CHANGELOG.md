# Changelog

All notable changes to ESP3D-Ender3 are documented in this file.

## [Unreleased]

### Added
- Custom `telegram_monitor` module for periodic Telegram print-status updates
- Professional README with installation, build, flashing, configuration, and troubleshooting guides
- CONTRIBUTING.md, CODE_OF_CONDUCT.md, SECURITY.md, CHANGELOG.md
- GitHub issue templates and pull request template
- GitHub Actions CI workflow (PlatformIO build)

### Changed
- Simplified `platformio.ini` to single `esp32_4MB_wifi` environment
- Updated `configuration.h` via ESP3D-Configurator V1.1.0

## [3.0.2] - Based on ESP3D v3.0

### Inherited from upstream ESP3D v3.0

- Wi-Fi station / AP mode with captive portal
- HTTP web interface, Telnet, WebSocket
- mDNS / SSDP discovery
- Notifications (Telegram, Email, Line, Pushover, IFTTT, Home Assistant)
- Firmware update via web / OTA / SD card
- FTP / WebDAV file access
- SD card support (Native SPI, SDIO, SDFat)
- OLED / TFT display support
- Sensors (DHT, BMX280, Analog)
- ESP32 Camera support (requires PSRAM)
- Lua interpreter (work in progress)
- Ethernet support (LAN8720)

### Changes from upstream ESP3D v3.0 in this fork

- New `telegram_monitor` module for periodic print-status Telegram notifications
- Integration of `telegram_monitor` into `Esp3D::begin()`, `Esp3D::handle()`, and serial data handling
- Simplified `platformio.ini` with single build environment (`esp32_4MB_wifi`)

## Upstream Releases

Official ESP3D releases are maintained at https://github.com/luc-github/ESP3D/releases

This fork's releases are independent of upstream releases. Version numbers follow the upstream ESP3D version with fork-specific suffixes when applicable.