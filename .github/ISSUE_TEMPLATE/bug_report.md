---
name: Bug Report
about: Report a bug or unexpected behavior in ESP3D-Ender3 firmware
title: "[BUG] "
labels: ["bug", "triage"]
assignees: ""
---

## Bug Description

A clear and concise description of what the bug is.

## To Reproduce

Steps to reproduce the behavior:

1. ...
2. ...
3. ...

## Expected Behavior

What you expected to happen.

## Actual Behavior

What actually happened.

## Environment

- **ESP3D version:** (check `info.json` or serial boot message)
- **Board:** (e.g., ESP32 Dev Module, ESP8266, etc.)
- **Flash size:** (e.g., 4 MB)
- **Printer model:** (e.g., Ender 3)
- **Printer firmware:** (e.g., Marlin 2.x.x)
- **Build environment:** (PlatformIO Core version, OS)
- **Build environment:** (`platformio.ini` environment used)

## Configuration

Please share relevant sections of `configuration.h` with **all passwords and credentials removed**:

```c
// Example - replace with your actual settings (no real passwords)
#define WIFI_FEATURE
#define HTTP_FEATURE
#define NOTIFICATION_FEATURE
```

## Build Logs / Serial Output

If applicable, paste build logs or serial monitor output here:

```
Paste logs here
```

## Additional Context

Add any other context about the problem here.

> **Security:** Do not share Wi-Fi passwords, API keys, tokens, or private network information in this issue.