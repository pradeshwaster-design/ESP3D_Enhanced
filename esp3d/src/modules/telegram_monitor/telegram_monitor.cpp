/*
  telegram_monitor.cpp - 2-Way Interactive Telegram Monitor & Remote Control

  V5 Improvements:
  1. Clickable single-tap commands (no spaces):
     - /preheat_pla, /preheat_petg, /home_all, /home_x, /home_y, /home_z
     - SD card files include direct /print_<n> clickable links
  2. Separate /temp (read-only query) vs /hotend <deg> (set extruder) vs /bed <deg> (set bed)
  3. Stricter validation rules:
     - /purge requires hotend >= 170C AND printer paused/prompt active
     - /continue requires printer paused or prompt active
     - /load and /unload blocked while printing OR if hotend < 170C
  4. Marlin Emergency Stop (M112) Halt handling:
     - Detects "Printer halted", "kill() called", "Error:STOPPED"
     - Enters _isKilled state, locks commands, and accurately informs user
       that Marlin hardware requires a power-cycle or /reboot (M999)
     - Automatically clears halt state when Marlin restarts (echo:start / SD card ok)
     - Added /reboot (M999) and /clear_halt commands
  5. Interactive, emoji-rich, user-friendly /help command
*/

#include "../../include/esp3d_config.h"
#if defined(NOTIFICATION_FEATURE)

#include "telegram_monitor.h"
#include "../notifications/notifications_service.h"
#include "../serial/serial_service.h"
#include <WiFiClientSecure.h>

TelegramMonitor telegram_monitor;

TelegramMonitor::TelegramMonitor() {
  _started = false;
  _skipFirstPoll = true;
  _lastPrintUpdateTime = 0;
  _lastIdleUpdateTime = 0;
  _lastPollTime = 0;
  _lastTelegramPollTime = 0;
  _filesRequestTime = 0;
  _lastUpdateId = 0;
  _pendingCommand = "";
  _pendingFromId = "";
  _isPrinting = false;
  _wasPrinting = false;
  _isPaused = false;
  _isKilled = false;
  _fileName = "";
  _hotendActual = 0;
  _hotendTarget = 0;
  _bedActual = 0;
  _bedTarget = 0;
  _progress = 0;
  _bytesDone = 0;
  _bytesTotal = 0;
  _posX = 0;
  _posY = 0;
  _zHeight = 0;
  _posE = 0;
  _feedrate = 100;
  _flowrate = 100;
  _printTime = "";
  _autoBreakWaits = false;
  _autoBreakCount = 0;
  _lastAutoM108Time = 0;
  _needAutoM108 = false;
  _marlinWaitingForUser = false;
  _marlinPrompt = "";
  _sdFileCount = 0;
  _collectingFiles = false;
  _pendingFilesRequest = false;
  _readyToSendFileList = false;
}

void TelegramMonitor::begin() {
  _started = true;
  _skipFirstPoll = true;
  uint32_t now = millis();
  _lastPrintUpdateTime = now;
  _lastIdleUpdateTime = now;
  _lastPollTime = now;
  _lastTelegramPollTime = now;
  _filesRequestTime = 0;
  _lastUpdateId = 0;
  _pendingCommand = "";
  _pendingFromId = "";
  _isPaused = false;
  _isKilled = false;
  _autoBreakWaits = false;
  _autoBreakCount = 0;
  _lastAutoM108Time = 0;
  _needAutoM108 = false;
  _marlinWaitingForUser = false;
  _marlinPrompt = "";
  _sdFileCount = 0;
  _collectingFiles = false;
  _pendingFilesRequest = false;
  _readyToSendFileList = false;
}

// ── Time helpers ─────────────────────────────────────────
static uint32_t parseElapsedSeconds(const String& t) {
  uint32_t s = 0;
  int hi = t.indexOf('h'), mi = t.indexOf('m'), si = t.indexOf('s');
  if (hi >= 0) { int p = 0; while (p < hi && !isDigit(t.charAt(p))) p++; s += t.substring(p, hi).toInt() * 3600; }
  if (mi >= 0) { int p = (hi >= 0) ? hi + 1 : 0; while (p < mi && !isDigit(t.charAt(p))) p++; s += t.substring(p, mi).toInt() * 60; }
  if (si >= 0) { int p = (mi >= 0) ? mi + 1 : ((hi >= 0) ? hi + 1 : 0); while (p < si && !isDigit(t.charAt(p))) p++; s += t.substring(p, si).toInt(); }
  return s;
}

static String formatTime(uint32_t sec) {
  if (sec == 0) return "Almost done";
  uint32_t h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
  if (h > 0) return String(h) + "h " + String(m) + "m";
  if (m > 0) return String(m) + "m " + String(s) + "s";
  return String(s) + "s";
}

static String buildProgressBar(float pct) {
  int f = (int)(pct / 10.0);
  if (f < 0) f = 0; if (f > 10) f = 10;
  String b = "[";
  for (int i = 0; i < 10; i++) b += (i < f) ? "#" : "-";
  b += "]";
  return b;
}

// ── Split serial data into lines ─────────────────────────
void TelegramMonitor::parseSerialData(const uint8_t* data, size_t size) {
  if (!_started || !data || size == 0) return;
  const char* p = (const char*)data;
  const char* end = p + size;
  while (p < end) {
    const char* start = p;
    while (p < end && *p != '\n' && *p != '\r') p++;
    size_t len = p - start;
    if (len > 0 && len < 256) {
      char buf[256];
      memcpy(buf, start, len);
      buf[len] = '\0';
      parseSingleLine(buf);
    }
    while (p < end && (*p == '\r' || *p == '\n')) p++;
  }
}

// ── Parse a single line from Marlin ──────────────────────
void TelegramMonitor::parseSingleLine(const char* line) {
  if (!line || line[0] == '\0') return;
  String d = String(line);
  d.trim();
  if (d.length() == 0) return;

  // --- Marlin HALT / KILL detection ---
  if (d.indexOf("Printer halted") >= 0 || d.indexOf("kill() called") >= 0 || 
      d.indexOf("//action:kill") >= 0 || d.indexOf("Error:STOPPED") >= 0) {
    _isKilled = true;
    _isPrinting = false;
    _isPaused = false;
    _wasPrinting = false;
    _autoBreakWaits = false;
    _autoBreakCount = 0;
    _marlinPrompt = "PRINTER HALTED - Please Reset";
    return;
  }

  // --- Marlin Boot / Restart detection ---
  if (d.indexOf("echo:start") >= 0 || d.indexOf("Marlin ") >= 0 || d.indexOf("SD card ok") >= 0) {
    _isKilled = false;
    _marlinWaitingForUser = false;
    _marlinPrompt = "";
  }

  // --- Temperature: "T:200.00 /200.00 B:50.00 /50.00" ---
  int tPos = d.indexOf("T:");
  if (tPos >= 0) {
    int sl = d.indexOf('/', tPos);
    if (sl > tPos) {
      _hotendActual = d.substring(tPos + 2, sl).toFloat();
      int e = sl + 1;
      while (e < (int)d.length() && d.charAt(e) != ' ' && d.charAt(e) != 'B' && d.charAt(e) != '@') e++;
      _hotendTarget = d.substring(sl + 1, e).toFloat();
    }
  }
  int bPos = d.indexOf("B:");
  if (bPos >= 0 && bPos != (tPos + 2)) {
    int sl = d.indexOf('/', bPos);
    if (sl > bPos) {
      _bedActual = d.substring(bPos + 2, sl).toFloat();
      int e = sl + 1;
      while (e < (int)d.length() && d.charAt(e) != ' ' && d.charAt(e) != '@') e++;
      _bedTarget = d.substring(sl + 1, e).toFloat();
    }
  }

  // --- Positions: "X:0.00 Y:235.00 Z:50.20 E:337.83" ---
  int xP = d.indexOf("X:");
  if (xP >= 0) { int e = d.indexOf(' ', xP + 2); if (e > xP + 2) _posX = d.substring(xP + 2, e).toFloat(); }
  int yP = d.indexOf("Y:");
  if (yP >= 0) { int e = d.indexOf(' ', yP + 2); if (e > yP + 2) _posY = d.substring(yP + 2, e).toFloat(); }
  int zP = d.indexOf("Z:");
  if (zP >= 0) { int e = d.indexOf(' ', zP + 2); if (e < 0) e = d.length(); _zHeight = d.substring(zP + 2, e).toFloat(); }
  int eP = d.indexOf("E:");
  if (eP >= 0) { int e = d.indexOf(' ', eP + 2); if (e < 0) e = d.length(); _posE = d.substring(eP + 2, e).toFloat(); }

  // --- Filename: "Current file: X.GCO" or "File opened: X.GCO Size: ..." ---
  if (d.indexOf("Current file: ") >= 0) {
    _fileName = d.substring(d.indexOf("Current file: ") + 14);
    _fileName.trim();
  }
  if (d.indexOf("File opened: ") >= 0) {
    int s = d.indexOf("File opened: ") + 13;
    int e = d.indexOf(' ', s);
    if (e < 0) e = d.length();
    _fileName = d.substring(s, e);
    _fileName.trim();
  }

  // --- SD progress: "SD printing byte 818544/2344893" ---
  int sdP = d.indexOf("SD printing byte ");
  if (sdP >= 0) {
    int ns = sdP + 17;
    int sl = d.indexOf('/', ns);
    if (sl > ns) {
      _bytesDone = d.substring(ns, sl).toInt();
      int e = sl + 1;
      while (e < (int)d.length() && isDigit(d.charAt(e))) e++;
      _bytesTotal = d.substring(sl + 1, e).toInt();
      if (_bytesTotal > 0) {
        _progress = ((float)_bytesDone / (float)_bytesTotal) * 100.0;
        _isPrinting = true;
        _isPaused = false;
        _autoBreakWaits = false;
        _autoBreakCount = 0;
        _marlinWaitingForUser = false;
        _marlinPrompt = "";
      }
    }
  }

  // --- "Not SD printing" ---
  if (d.indexOf("Not SD printing") >= 0) {
    if (!_isPaused && !_autoBreakWaits) {
      _isPrinting = false;
    }
  }

  // --- Print time: "echo:Print time: 53m 52s" ---
  int ptP = d.indexOf("Print time: ");
  if (ptP >= 0) {
    _printTime = d.substring(ptP + 12);
    _printTime.trim();
    if (_printTime.length() > 20) _printTime = _printTime.substring(0, 20);
  }

  // --- Feedrate: "FR:100%" ---
  int frP = d.indexOf("FR:");
  if (frP >= 0) {
    int pp = d.indexOf('%', frP);
    if (pp > frP + 3) _feedrate = d.substring(frP + 3, pp).toInt();
  }

  // --- Flow: "Flow: 100%" ---
  int flP = d.indexOf("Flow: ");
  if (flP < 0) flP = d.indexOf("flow: ");
  if (flP >= 0) {
    int pp = d.indexOf('%', flP);
    if (pp > flP + 6) _flowrate = d.substring(flP + 6, pp).toInt();
  }

  // --- Marlin keepalive "busy: paused for user" ---
  if (d.indexOf("paused for user") >= 0) {
    _marlinWaitingForUser = true;
    _isPaused = true;
    _isPrinting = true;
    if (_autoBreakWaits && _autoBreakCount > 0) {
      _needAutoM108 = true;
    }
  }

  // --- "busy: processing" ---
  if (d.indexOf("busy: processing") >= 0) {
    _marlinWaitingForUser = false;
  }

  // --- Host action & prompt messages (//action:*) ---
  if (d.startsWith("//action:")) {
    String action = d.substring(9);
    action.trim();

    if (action.startsWith("prompt_begin ")) {
      _marlinPrompt = action.substring(13);
      _marlinPrompt.trim();
      _marlinWaitingForUser = true;
      if (_autoBreakWaits && _autoBreakCount > 0) {
        _needAutoM108 = true;
      }
    }
    else if (action.startsWith("prompt_button ")) {
      String btn = action.substring(14);
      btn.trim();
      if (_marlinPrompt.length() > 0) {
        _marlinPrompt += " | " + btn;
      } else {
        _marlinPrompt = btn;
      }
      _marlinWaitingForUser = true;
      if (_autoBreakWaits && _autoBreakCount > 0) {
        _needAutoM108 = true;
      }
    }
    else if (action == "prompt_show") {
      _marlinWaitingForUser = true;
      if (_autoBreakWaits && _autoBreakCount > 0) {
        _needAutoM108 = true;
      }
    }
    else if (action == "prompt_end") {
      _marlinPrompt = "";
      _marlinWaitingForUser = false;
    }
    else if (action == "paused" || action == "pause") {
      _isPaused = true;
      _isPrinting = true;
    }
    else if (action == "resumed" || action == "resume") {
      _isPaused = false;
      _isPrinting = true;
      _autoBreakWaits = false;
      _autoBreakCount = 0;
      _marlinWaitingForUser = false;
      _marlinPrompt = "";
    }
    else if (action == "cancel") {
      _isPaused = false;
      _isPrinting = false;
      _autoBreakWaits = false;
      _autoBreakCount = 0;
      _marlinWaitingForUser = false;
      _marlinPrompt = "";
    }
  }

  // --- SD file list (from M20) ---
  if (d.indexOf("Begin file list") >= 0) {
    _collectingFiles = true;
    _sdFileCount = 0;
    return;
  }
  if (d.indexOf("End file list") >= 0) {
    _collectingFiles = false;
    if (_pendingFilesRequest) {
      _readyToSendFileList = true;
    }
    return;
  }
  if (_collectingFiles) {
    String fname = d;
    fname.trim();
    if (fname.length() > 0 && !fname.startsWith("ok") && !fname.startsWith("//") && !fname.startsWith("echo:")) {
      int sp = fname.indexOf(' ');
      if (sp > 0) fname = fname.substring(0, sp);
      if (_sdFileCount < MAX_SD_FILES) {
        _sdFiles[_sdFileCount++] = fname;
      }
    }
  }
}

// ── Outgoing messages ────────────────────────────────────
void TelegramMonitor::sendTelegramReply(const String& text) {
  notificationsservice.sendMSG("ESP3D", text.c_str());
}

void TelegramMonitor::sendStatusUpdate() {
  if (!notificationsservice.started()) return;

  if (_isKilled) {
    sendTelegramReply("🚨 PRINTER HALTED / KILLED!\n\nSafety shutdown active.\nPhysical reset required: Turn printer power switch OFF and ON.");
    return;
  }

  uint32_t elapsed = parseElapsedSeconds(_printTime);
  String timeLeft = "Calculating...";
  if (elapsed > 10 && _progress > 1.0 && _progress < 99.5) {
    uint32_t rem = (uint32_t)((float)elapsed / (_progress / 100.0) - elapsed);
    timeLeft = formatTime(rem);
  }
  String msg = "Print Status\n";
  if (_fileName.length() > 0) msg += "File: " + _fileName + "\n";
  msg += buildProgressBar(_progress) + " " + String(_progress, 1) + "%\n";
  if (_isPaused) {
    if (_marlinWaitingForUser && _marlinPrompt.length() > 0) {
      msg += "PAUSED - LCD: " + _marlinPrompt + "\n";
    } else if (_marlinWaitingForUser) {
      msg += "PAUSED (waiting for user)\n";
    } else {
      msg += "PAUSED (processing...)\n";
    }
  }
  if (_printTime.length() > 0) msg += "Elapsed: " + _printTime + "\n";
  msg += "Time Left: ~" + timeLeft + "\n";
  msg += "Hotend: " + String(_hotendActual, 0) + "/" + String(_hotendTarget, 0) + "C\n";
  msg += "Bed: " + String(_bedActual, 0) + "/" + String(_bedTarget, 0) + "C\n";
  msg += "Z: " + String(_zHeight, 1) + "mm";
  notificationsservice.sendMSG("ESP3D", msg.c_str());
}

void TelegramMonitor::sendIdleStatus() {
  if (!notificationsservice.started()) return;

  if (_isKilled) {
    sendTelegramReply("🚨 PRINTER HALTED / KILLED!\n\nSafety shutdown active.\nPhysical reset required: Turn printer power switch OFF and ON.");
    return;
  }

  String msg = "Printer Status: Idle\n";
  msg += "Hotend: " + String(_hotendActual, 0) + "/" + String(_hotendTarget, 0) + "C\n";
  msg += "Bed: " + String(_bedActual, 0) + "/" + String(_bedTarget, 0) + "C";
  notificationsservice.sendMSG("ESP3D", msg.c_str());
}

void TelegramMonitor::sendPrintStarted() {
  if (!notificationsservice.started()) return;
  String msg = "Print Started!\n";
  if (_fileName.length() > 0) msg += "File: " + _fileName + "\n";
  msg += "Hotend: " + String(_hotendTarget, 0) + "C | Bed: " + String(_bedTarget, 0) + "C";
  notificationsservice.sendMSG("ESP3D", msg.c_str());
}

void TelegramMonitor::sendPrintFinished() {
  if (!notificationsservice.started()) return;
  String msg = "Print Finished!\n";
  if (_fileName.length() > 0) msg += "File: " + _fileName + "\n";
  if (_printTime.length() > 0) msg += "Total Time: " + _printTime + "\n";
  msg += "Hotend: " + String(_hotendActual, 0) + "C | Bed: " + String(_bedActual, 0) + "C";
  notificationsservice.sendMSG("ESP3D", msg.c_str());
}

void TelegramMonitor::sendSDFileListReply() {
  if (_sdFileCount == 0) {
    sendTelegramReply("SD Card: No files found or card not inserted.");
    return;
  }
  String msg = "📁 SD Card Files (" + String(_sdFileCount) + "):\n\n";
  for (uint8_t i = 0; i < _sdFileCount; i++) {
    msg += String(i + 1) + ". " + _sdFiles[i] + "\n";
    msg += "   ▶️ /print_" + String(i + 1) + "\n\n";
  }
  msg += "Tap any /print_<n> link above to start printing!";
  sendTelegramReply(msg);
}

// ── Command processing ───────────────────────────────────
void TelegramMonitor::processTelegramCommand(String cmd, const String& fromId) {
  const String& authChatId = notificationsservice.getToken2();
  if (authChatId.length() > 0 && fromId.length() > 0 && fromId != authChatId) return;

  cmd.trim();
  int atIdx = cmd.indexOf('@');
  if (atIdx >= 0) cmd = cmd.substring(0, atIdx);

  String lc = cmd;
  lc.toLowerCase();

  // ── Recovery Commands (Allowed even when halted) ──
  if (lc == "/reboot" || lc == "/reset") {
    esp3d_serial_service.writeBytes((const uint8_t*)"M999\n", 5);
    _isKilled = false;
    _marlinPrompt = "";
    _marlinWaitingForUser = false;
    sendTelegramReply("Sent M999 (Restart / Clear Stop).\nIf printer LCD still displays 'Please Reset', turn printer power switch OFF and ON.");
    return;
  }
  else if (lc == "/clear_halt" || lc == "/clear") {
    _isKilled = false;
    _marlinPrompt = "";
    _marlinWaitingForUser = false;
    sendTelegramReply("Halt state cleared manually.\nPrinter state set to Idle.");
    return;
  }

  // ── Emergency Stop (Always allowed) ──
  if (lc == "/emergency") {
    esp3d_serial_service.writeBytes((const uint8_t*)"M112\n", 5);
    _isKilled = true;
    _isPaused = false;
    _isPrinting = false;
    _wasPrinting = false;
    _autoBreakWaits = false;
    _autoBreakCount = 0;
    _marlinPrompt = "PRINTER HALTED - Please Reset";
    sendTelegramReply("🚨 EMERGENCY STOP (M112 sent)!\n\nAll motors and heaters killed immediately.\n⚠️ Marlin is now HALTED. Printer hardware is locked.\n👉 Turn printer power switch OFF and ON to recover, or send /reboot.");
    return;
  }

  // ── Read-only Status Commands (Allowed even when halted) ──
  if (lc == "/status") {
    if (_isKilled) {
      sendTelegramReply("🚨 PRINTER HALTED / KILLED!\n\nSafety shutdown active.\nPhysical reset required: Turn printer power switch OFF and ON, or send /reboot.");
    } else if (_isPrinting || _isPaused) {
      sendStatusUpdate();
    } else {
      sendIdleStatus();
    }
    return;
  }
  else if (lc == "/temp") {
    String msg = "Temperatures:\n";
    msg += "Hotend: " + String(_hotendActual, 1) + " / " + String(_hotendTarget, 0) + "C\n";
    msg += "Bed: " + String(_bedActual, 1) + " / " + String(_bedTarget, 0) + "C";
    sendTelegramReply(msg);
    return;
  }
  else if (lc == "/pos" || lc == "/position") {
    if (!_isKilled) {
      esp3d_serial_service.writeBytes((const uint8_t*)"M114\n", 5);
    }
    String msg = "Tool Head Position:\n";
    msg += "X: " + String(_posX, 2) + " mm\n";
    msg += "Y: " + String(_posY, 2) + " mm\n";
    msg += "Z: " + String(_zHeight, 2) + " mm\n";
    msg += "E: " + String(_posE, 2) + " mm";
    sendTelegramReply(msg);
    return;
  }
  else if (lc == "/display" || lc == "/screen" || lc == "/lcd") {
    if (_isKilled) {
      sendTelegramReply("Printer Screen:\nState: 🚨 HALTED (Reset Required)\nLCD: PRINTER HALTED / Please Reset\n\nTurn power switch OFF then ON to recover.");
      return;
    }
    String state = "Idle";
    if (_isPaused && _marlinWaitingForUser) state = "WAITING FOR USER";
    else if (_isPaused) state = "PAUSED";
    else if (_isPrinting) state = "Printing";

    String msg = "Printer Screen:\n";
    msg += "State: " + state + "\n";

    if (_marlinWaitingForUser && _marlinPrompt.length() > 0) {
      msg += "Active Prompt: " + _marlinPrompt + "\n";
    }

    if (_isPrinting || _isPaused) {
      if (_fileName.length() > 0) msg += "File: " + _fileName + "\n";
      msg += "Progress: " + String(_progress, 1) + "%\n";
      if (_printTime.length() > 0) msg += "Elapsed: " + _printTime + "\n";
    }
    msg += "Hotend: " + String(_hotendActual, 0) + "/" + String(_hotendTarget, 0) + "C\n";
    msg += "Bed: " + String(_bedActual, 0) + "/" + String(_bedTarget, 0) + "C\n";
    msg += "Z: " + String(_zHeight, 2) + " mm\n";
    msg += "Speed: " + String(_feedrate) + "% | Flow: " + String(_flowrate) + "%";
    sendTelegramReply(msg);
    return;
  }
  else if (lc == "/files" || lc == "/ls") {
    _collectingFiles = false;
    _sdFileCount = 0;
    _pendingFilesRequest = true;
    _readyToSendFileList = false;
    _filesRequestTime = millis();
    esp3d_serial_service.writeBytes((const uint8_t*)"M21\nM20\n", 8);
    return;
  }
  else if (lc == "/help" || lc == "/start") {
    String msg = "🎮 3D Printer Remote Control\n\n";
    msg += "📊 MONITORING:\n";
    msg += "/status — Live progress & temps\n";
    msg += "/temp — Check temperatures\n";
    msg += "/pos — Toolhead coordinates\n";
    msg += "/display — LCD info & prompt\n";
    msg += "/files — Browse SD card files\n\n";
    msg += "⏯️ PRINT CONTROL:\n";
    msg += "/pause — Pause print safely\n";
    msg += "/resume — Resume print\n";
    msg += "/stop — Cancel print & cool down\n";
    msg += "/continue — Select 'Continue'\n";
    msg += "/purge — Select 'Purge More'\n";
    msg += "/emergency — 🚨 Instant kill stop\n\n";
    msg += "🔥 TEMPERATURE:\n";
    msg += "/preheat_pla — 200C Hotend / 60C Bed\n";
    msg += "/preheat_petg — 240C Hotend / 75C Bed\n";
    msg += "/cooldown — Turn off heaters\n";
    msg += "/hotend 200 — Set hotend temp\n";
    msg += "/bed 60 — Set bed temp\n\n";
    msg += "🧵 FILAMENT (Idle or Paused):\n";
    msg += "/load — Extrude 60mm\n";
    msg += "/unload — Retract 430mm\n\n";
    msg += "🧭 MOTION:\n";
    msg += "/home_all — Home all axes\n";
    msg += "/home_x — Home X axis\n";
    msg += "/home_y — Home Y axis\n";
    msg += "/home_z — Home Z axis\n\n";
    msg += "🔄 RECOVERY:\n";
    msg += "/reboot — Clear error (M999)\n";
    msg += "/clear_halt — Clear halt state";
    sendTelegramReply(msg);
    return;
  }

  // ──────────────────────────────────────────────────────────
  // All operational commands below REQUIRE printer NOT halted!
  // ──────────────────────────────────────────────────────────
  if (_isKilled) {
    sendTelegramReply("⚠️ Printer is in EMERGENCY HALTED state!\nCommands are locked.\nPlease power cycle the printer (turn switch OFF then ON) to reset.");
    return;
  }

  // ── Hotend Temperature: /hotend <deg> or /hotend_<deg> ──
  if (lc.startsWith("/hotend ") || lc.startsWith("/hotend_") || lc.startsWith("/settemp ") || lc.startsWith("/settemp_")) {
    int sp = cmd.indexOf(' ');
    if (sp < 0) sp = cmd.indexOf('_');
    String arg = cmd.substring(sp + 1);
    arg.trim();
    float target = arg.toFloat();
    if (target < 0 || target > 260.0) {
      sendTelegramReply("Invalid hotend temp! Range: 0 to 260C (safe PTFE limit).");
      return;
    }
    String gcode = "M104 S" + String((int)target) + "\n";
    esp3d_serial_service.writeBytes((const uint8_t*)gcode.c_str(), gcode.length());
    _hotendTarget = target;
    sendTelegramReply("Hotend target set to " + String((int)target) + "C");
  }
  // ── Bed Temperature: /bed <deg> or /bed_<deg> ──
  else if (lc.startsWith("/bed ") || lc.startsWith("/bed_") || lc.startsWith("/setbed ") || lc.startsWith("/setbed_")) {
    int sp = cmd.indexOf(' ');
    if (sp < 0) sp = cmd.indexOf('_');
    String arg = cmd.substring(sp + 1);
    arg.trim();
    float target = arg.toFloat();
    if (target < 0 || target > 100.0) {
      sendTelegramReply("Invalid bed temp! Range: 0 to 100C.");
      return;
    }
    String gcode = "M140 S" + String((int)target) + "\n";
    esp3d_serial_service.writeBytes((const uint8_t*)gcode.c_str(), gcode.length());
    _bedTarget = target;
    sendTelegramReply("Bed target set to " + String((int)target) + "C");
  }
  // ── Preheat PLA (Hotend 200C / Bed 60C) ──
  else if (lc == "/preheat_pla" || lc == "/preheat pla" || lc == "/pla") {
    esp3d_serial_service.writeBytes((const uint8_t*)"M104 S200\nM140 S60\n", 20);
    _hotendTarget = 200;
    _bedTarget = 60;
    sendTelegramReply("Preheating PLA:\nHotend: 200C | Bed: 60C");
  }
  // ── Preheat PETG (Hotend 240C / Bed 75C) ──
  else if (lc == "/preheat_petg" || lc == "/preheat petg" || lc == "/petg") {
    esp3d_serial_service.writeBytes((const uint8_t*)"M104 S240\nM140 S75\n", 20);
    _hotendTarget = 240;
    _bedTarget = 75;
    sendTelegramReply("Preheating PETG:\nHotend: 240C | Bed: 75C");
  }
  // ── Cooldown / Off ──
  else if (lc == "/cooldown" || lc == "/off") {
    esp3d_serial_service.writeBytes((const uint8_t*)"M104 S0\nM140 S0\n", 16);
    _hotendTarget = 0;
    _bedTarget = 0;
    sendTelegramReply("Cooldown: Hotend and Bed heaters turned OFF");
  }
  // ── Filament: Load ──
  else if (lc == "/load" || lc == "/loadfilament") {
    if (_isPrinting && !_isPaused) {
      sendTelegramReply("Cannot load filament while actively printing!\nPause the print first with /pause.");
      return;
    }
    if (_hotendActual < 170.0) {
      sendTelegramReply("Hotend is too cold (" + String(_hotendActual, 0) + "C)!\nCold extrusion is prevented.\nHeat hotend to at least 180C first (use /preheat_pla or /hotend 200).");
      return;
    }
    esp3d_serial_service.writeBytes((const uint8_t*)"G91\nM83\nG1 E60 F150\nG90\n", 27);
    sendTelegramReply("Loading filament...\nExtruding 60mm at 150 mm/min.\nSend /load again if more purge is needed.");
  }
  // ── Filament: Unload ──
  else if (lc == "/unload" || lc == "/unloadfilament") {
    if (_isPrinting && !_isPaused) {
      sendTelegramReply("Cannot unload filament while actively printing!\nPause the print first with /pause.");
      return;
    }
    if (_hotendActual < 170.0) {
      sendTelegramReply("Hotend is too cold (" + String(_hotendActual, 0) + "C)!\nCold retraction is prevented.\nHeat hotend to at least 180C first (use /preheat_pla or /hotend 200).");
      return;
    }
    esp3d_serial_service.writeBytes((const uint8_t*)"G91\nM83\nG1 E10 F200\nG1 E-80 F1500\nG1 E-350 F2500\nG90\n", 51);
    sendTelegramReply("Unloading filament...\nMelting tip and retracting 430mm out of the Bowden tube.");
  }
  // ── Prompt Response: Continue ──
  else if (lc == "/continue") {
    if (!_isPaused && !_marlinWaitingForUser && _marlinPrompt.length() == 0) {
      sendTelegramReply("Nothing to continue - printer is not paused and no prompt is active.");
      return;
    }
    esp3d_serial_service.writeBytes((const uint8_t*)"M876 S1\nM108\n", 13);
    sendTelegramReply("Prompt response sent: Continue (M876 S1 + M108)");
  }
  // ── Prompt Response: Purge More ──
  else if (lc == "/purge") {
    if (_isPrinting && !_isPaused) {
      sendTelegramReply("Cannot purge while actively printing!");
      return;
    }
    if (_hotendActual < 170.0) {
      sendTelegramReply("Hotend is too cold (" + String(_hotendActual, 0) + "C)!\nPurge requires at least 180C.\nHeat hotend first (/preheat_pla or /hotend 200).");
      return;
    }
    if (!_isPaused && !_marlinWaitingForUser && _marlinPrompt.length() == 0) {
      sendTelegramReply("Nothing to purge - printer is idle and no prompt is active.");
      return;
    }
    esp3d_serial_service.writeBytes((const uint8_t*)"M876 S0\n", 8);
    sendTelegramReply("Prompt response sent: Purge More (M876 S0)");
  }
  // ── Motion: Home ──
  else if (lc == "/home" || lc == "/home_all") {
    if (_isPrinting && !_isPaused) {
      sendTelegramReply("Cannot home while actively printing!");
      return;
    }
    esp3d_serial_service.writeBytes((const uint8_t*)"G28\n", 4);
    sendTelegramReply("Homing all axes (G28 sent)");
  } else if (lc == "/home_x" || lc == "/home x") {
    if (_isPrinting && !_isPaused) { sendTelegramReply("Cannot home while printing!"); return; }
    esp3d_serial_service.writeBytes((const uint8_t*)"G28 X\n", 6);
    sendTelegramReply("Homing X axis");
  } else if (lc == "/home_y" || lc == "/home y") {
    if (_isPrinting && !_isPaused) { sendTelegramReply("Cannot home while printing!"); return; }
    esp3d_serial_service.writeBytes((const uint8_t*)"G28 Y\n", 6);
    sendTelegramReply("Homing Y axis");
  } else if (lc == "/home_z" || lc == "/home z") {
    if (_isPrinting && !_isPaused) { sendTelegramReply("Cannot home while printing!"); return; }
    esp3d_serial_service.writeBytes((const uint8_t*)"G28 Z\n", 6);
    sendTelegramReply("Homing Z axis");
  }
  // ── Pause (M25) ──
  else if (lc == "/pause") {
    if (!_isPrinting && !_isPaused) {
      sendTelegramReply("Nothing to pause - printer is idle.");
      return;
    }
    if (_isPaused) {
      sendTelegramReply("Already paused. Send /resume to continue.");
      return;
    }
    esp3d_serial_service.writeBytes((const uint8_t*)"M25\n", 4);
    _isPaused = true;
    _autoBreakWaits = false;
    sendTelegramReply("Print Paused (M25 sent)\nNozzle will park safely.\nSend /resume to continue or /stop to cancel.");
  }
  // ── Resume ──
  else if (lc == "/resume") {
    if (!_isPaused) {
      sendTelegramReply("Printer is not paused. Nothing to resume.");
      return;
    }
    esp3d_serial_service.writeBytes((const uint8_t*)"M876 S1\nM108\nM24\n", 17);
    _autoBreakWaits = true;
    _autoBreakCount = AUTO_M108_MAX_COUNT;
    _lastAutoM108Time = millis();
    _needAutoM108 = false;
    _marlinWaitingForUser = false;
    sendTelegramReply("Resuming print...\nSent M876 S1 + M108 + M24.\nAuto-skipping Purge/Continue prompt.");
  }
  // ── Stop (M876 S1 + M108 + M524 + Cooldown) ──
  else if (lc == "/stop") {
    if (!_isPrinting && !_isPaused) {
      sendTelegramReply("Nothing to stop - printer is idle.");
      return;
    }
    esp3d_serial_service.writeBytes((const uint8_t*)"M876 S1\nM108\nM524\nM104 S0\nM140 S0\n", 29);
    _autoBreakWaits = false;
    _autoBreakCount = 0;
    _needAutoM108 = false;
    _isPaused = false;
    _isPrinting = false;
    _wasPrinting = false;
    _progress = 0;
    _marlinPrompt = "";
    _marlinWaitingForUser = false;
    sendTelegramReply("Print Cancelled (M876 S1 + M108 + M524 sent)\nHeaters turning off.\nPrinter will return to idle.");
  }
  // ── Print a file: /print <n|file> or /print_<n> ──
  else if (lc.startsWith("/print_") || lc.startsWith("/print ") || lc == "/print") {
    String arg;
    if (lc.startsWith("/print_")) {
      arg = cmd.substring(7);
    } else if (lc.startsWith("/print ")) {
      arg = cmd.substring(7);
    } else {
      sendTelegramReply("Usage: /print_1 or /print <filename>\nType /files to see list.");
      return;
    }
    arg.trim();
    if (arg.length() == 0) {
      sendTelegramReply("Usage: /print_1 or /print <filename>\nType /files to see list.");
      return;
    }
    if (_isPrinting || _isPaused) {
      sendTelegramReply("Printer is busy! Stop current print first with /stop.");
      return;
    }
    String selectedFile = "";
    bool isNum = true;
    for (size_t i = 0; i < arg.length(); i++) {
      if (!isDigit(arg.charAt(i))) { isNum = false; break; }
    }
    if (isNum) {
      int idx = arg.toInt() - 1;
      if (idx >= 0 && idx < _sdFileCount) {
        selectedFile = _sdFiles[idx];
      } else {
        sendTelegramReply("Invalid file number. Type /files to see available files.");
        return;
      }
    } else {
      selectedFile = arg;
    }
    String m23 = "M23 " + selectedFile + "\n";
    esp3d_serial_service.writeBytes((const uint8_t*)m23.c_str(), m23.length());
    delay(100);
    esp3d_serial_service.writeBytes((const uint8_t*)"M24\n", 4);
    _fileName = selectedFile;
    _progress = 0;
    _printTime = "";
    sendTelegramReply("Starting print: " + selectedFile);
  }
  else if (lc.startsWith("/")) {
    sendTelegramReply("Unknown command. Type /help for list.");
  }
}

// ── Poll Telegram for incoming commands ──────────────────
void TelegramMonitor::pollTelegramUpdates() {
  if (!notificationsservice.started()) return;
  if (notificationsservice.getNotificationType() != ESP_TELEGRAM_NOTIFICATION) return;

  const String& token = notificationsservice.getToken1();
  const String& chatId = notificationsservice.getToken2();
  if (token.length() == 0 || chatId.length() == 0) return;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(2);

  if (!client.connect("api.telegram.org", 443)) return;

  String body = "offset=" + String((long long)(_lastUpdateId + 1)) + "&limit=3&timeout=0";
  String req = "POST /bot" + token + "/getUpdates HTTP/1.1\r\n"
               "Host: api.telegram.org\r\n"
               "Connection: close\r\n"
               "Content-Type: application/x-www-form-urlencoded\r\n"
               "Content-Length: " + String(body.length()) + "\r\n\r\n" + body;
  client.print(req);

  uint32_t t0 = millis();
  while (!client.available() && (millis() - t0 < 2000)) delay(10);
  if (!client.available()) { client.stop(); return; }

  bool inBody = false;
  String resp = "";
  while (client.connected() || client.available()) {
    char c = client.read();
    if (c == (char)-1) continue;
    resp += c;
    if (!inBody) {
      if (resp.endsWith("\r\n\r\n")) { inBody = true; resp = ""; }
    } else {
      if (resp.length() > 4096) break;
    }
  }
  client.stop();

  if (!inBody || resp.length() == 0) return;

  // Parse updates
  int idx = 0;
  while ((idx = resp.indexOf("\"update_id\":", idx)) >= 0) {
    int idS = idx + 12, idE = idS;
    while (idE < (int)resp.length() && (isDigit(resp.charAt(idE)) || resp.charAt(idE) == '-')) idE++;
    int64_t uid = atoll(resp.substring(idS, idE).c_str());
    if (uid > _lastUpdateId) _lastUpdateId = uid;

    int next = resp.indexOf("\"update_id\":", idE);
    if (next < 0) next = resp.length();
    String chunk = resp.substring(idE, next);

    // Extract chat id
    String fromId = "";
    int cp = chunk.indexOf("\"chat\":{");
    if (cp >= 0) {
      int ip = chunk.indexOf("\"id\":", cp);
      if (ip >= 0) {
        int s = ip + 5, e = s;
        while (e < (int)chunk.length() && (isDigit(chunk.charAt(e)) || chunk.charAt(e) == '-')) e++;
        fromId = chunk.substring(s, e);
      }
    }

    // Extract text
    int tp = chunk.indexOf("\"text\":\"");
    if (tp >= 0) {
      int ts = tp + 8, te = chunk.indexOf('"', ts);
      if (te > ts) {
        String cmdText = chunk.substring(ts, te);
        if (!_skipFirstPoll) {
          _pendingCommand = cmdText;
          _pendingFromId = fromId;
        }
      }
    }
    idx = next;
  }

  if (_skipFirstPoll) _skipFirstPoll = false;
}

// ── Main loop handler ────────────────────────────────────
void TelegramMonitor::handle() {
  if (!_started) return;
  uint32_t now = millis();

  // ────────────────────────────────────────────────────
  // PRIORITY 0: Auto-respond to Marlin prompts & wait loops
  // Sends M876 S1 (select Continue) + M108 (break wait)
  // ────────────────────────────────────────────────────
  if (_autoBreakWaits && _needAutoM108 && _autoBreakCount > 0) {
    if (now - _lastAutoM108Time >= AUTO_M108_COOLDOWN) {
      esp3d_serial_service.writeBytes((const uint8_t*)"M876 S1\nM108\n", 13);
      _autoBreakCount--;
      _lastAutoM108Time = now;
      _needAutoM108 = false;
      _marlinWaitingForUser = false;
      if (_autoBreakCount == 0) {
        _autoBreakWaits = false;
      }
    }
  }

  // Timeout: if 30s pass with autoBreakWaits still active, stop to prevent loops
  if (_autoBreakWaits && (now - _lastAutoM108Time >= 30000)) {
    _autoBreakWaits = false;
    _autoBreakCount = 0;
    _needAutoM108 = false;
  }

  // 1. Execute queued command (safe: SSL connection already closed)
  if (_pendingCommand.length() > 0) {
    String cmd = _pendingCommand;
    String fid = _pendingFromId;
    _pendingCommand = "";
    _pendingFromId = "";
    processTelegramCommand(cmd, fid);
    return;
  }

  // 2. Send file list reply (deferred from serial ISR)
  if (_pendingFilesRequest && _readyToSendFileList) {
    _pendingFilesRequest = false;
    _readyToSendFileList = false;
    _collectingFiles = false;
    sendSDFileListReply();
    return;
  }

  // 3. File list timeout (4 seconds)
  if (_pendingFilesRequest && (now - _filesRequestTime >= 4000)) {
    _pendingFilesRequest = false;
    _readyToSendFileList = false;
    _collectingFiles = false;
    sendSDFileListReply();
    return;
  }

  // 4. Poll Telegram every 4 seconds
  if (now - _lastTelegramPollTime >= TELEGRAM_POLL_INTERVAL) {
    _lastTelegramPollTime = now;
    pollTelegramUpdates();
  }

  // 5. Poll Marlin for live data
  if (_isPrinting || _isPaused) {
    if (now - _lastPollTime >= 12000) {
      _lastPollTime = now;
      if (_fileName.length() == 0) {
        esp3d_serial_service.writeBytes((const uint8_t*)"M27 C\n", 6);
      }
      esp3d_serial_service.writeBytes((const uint8_t*)"M31\n", 4);
    }
  } else if (!_isKilled) {
    if (now - _lastPollTime >= 30000) {
      _lastPollTime = now;
      esp3d_serial_service.writeBytes((const uint8_t*)"M105\n", 5);
    }
  }

  // 6. Detect print start
  if (_isPrinting && !_wasPrinting && !_isPaused && !_isKilled) {
    sendPrintStarted();
    _lastPrintUpdateTime = now;
    _wasPrinting = true;
    esp3d_serial_service.writeBytes((const uint8_t*)"M27 C\n", 6);
    esp3d_serial_service.writeBytes((const uint8_t*)"M31\n", 4);
  }

  // 7. Detect print finished (only when NOT paused and NOT auto-breaking)
  if (!_isPrinting && !_isPaused && !_autoBreakWaits && !_isKilled && _wasPrinting) {
    sendPrintFinished();
    _wasPrinting = false;
    _progress = 0;
    _printTime = "";
    _fileName = "";
    _marlinPrompt = "";
    _lastIdleUpdateTime = now;
  }

  // 8. Periodic print update (every 1 minute)
  if ((_isPrinting || _isPaused) && !_isKilled && (now - _lastPrintUpdateTime >= TELEGRAM_PRINT_INTERVAL)) {
    sendStatusUpdate();
    _lastPrintUpdateTime = now;
  }

  // 9. Periodic idle update (every 5 minutes)
  if (!_isPrinting && !_isPaused && !_autoBreakWaits && !_isKilled && (now - _lastIdleUpdateTime >= TELEGRAM_IDLE_INTERVAL)) {
    sendIdleStatus();
    _lastIdleUpdateTime = now;
  }
}

#endif  // NOTIFICATION_FEATURE
