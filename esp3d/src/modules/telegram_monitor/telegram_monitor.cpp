/*
  telegram_monitor.cpp - 2-Way Interactive Telegram Monitor & Remote Control

  Bug fixes applied:
  1. Skip stale messages on boot (no double-reboot from old /files or /print)
  2. Pause state tracking (_isPaused) - no false "Print Finished" on M25 pause
  3. Correct Marlin commands: M108 to resume (breaks wait_for_user), M108+M524 to stop
  4. All network calls deferred to handle() - never called from serial ISR
  5. Proper line-by-line serial parsing with size-aware buffer
  6. Detection of "busy: paused for user" keepalive from Marlin
  7. URL-safe message content (& encoded in notifications_service.cpp)
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
        _isPaused = false;  // If we see SD progress, we are definitely not paused
      }
    }
  }

  // --- "Not SD printing" ---
  // BUG FIX #3: Only mark as not-printing if we are NOT paused
  if (d.indexOf("Not SD printing") >= 0) {
    if (!_isPaused) {
      _isPrinting = false;
    }
    // If paused, ignore this — Marlin sends "Not SD printing" while paused
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

  // --- BUG FIX #7: Detect "busy: paused for user" keepalive ---
  if (d.indexOf("paused for user") >= 0) {
    _isPaused = true;
    _isPrinting = true;  // Still considered "printing" (just paused)
  }

  // --- Detect action:paused from host action ---
  if (d.indexOf("//action:paused") >= 0 || d.indexOf("//action:pause") >= 0) {
    _isPaused = true;
    _isPrinting = true;
  }

  // --- Detect resume from action ---
  if (d.indexOf("//action:resumed") >= 0 || d.indexOf("//action:resume") >= 0) {
    _isPaused = false;
    _isPrinting = true;
  }

  // --- SD file list (from M20): ---
  if (d.indexOf("Begin file list") >= 0) {
    _collectingFiles = true;
    _sdFileCount = 0;
    return;
  }
  if (d.indexOf("End file list") >= 0) {
    _collectingFiles = false;
    if (_pendingFilesRequest) {
      _readyToSendFileList = true;  // Deferred to handle() — NEVER call network from ISR
    }
    return;
  }
  if (_collectingFiles) {
    String fname = d;
    fname.trim();
    if (fname.length() > 0 && !fname.startsWith("ok") && !fname.startsWith("//") && !fname.startsWith("echo:")) {
      int sp = fname.indexOf(' ');
      if (sp > 0) fname = fname.substring(0, sp);  // Remove trailing file size
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
  uint32_t elapsed = parseElapsedSeconds(_printTime);
  String timeLeft = "Calculating...";
  if (elapsed > 10 && _progress > 1.0 && _progress < 99.5) {
    uint32_t rem = (uint32_t)((float)elapsed / (_progress / 100.0) - elapsed);
    timeLeft = formatTime(rem);
  }
  String msg = "Print Status\n";
  if (_fileName.length() > 0) msg += "File: " + _fileName + "\n";
  msg += buildProgressBar(_progress) + " " + String(_progress, 1) + "%\n";
  if (_isPaused) msg += "** PAUSED **\n";
  if (_printTime.length() > 0) msg += "Elapsed: " + _printTime + "\n";
  msg += "Time Left: ~" + timeLeft + "\n";
  msg += "Hotend: " + String(_hotendActual, 0) + "/" + String(_hotendTarget, 0) + "C\n";
  msg += "Bed: " + String(_bedActual, 0) + "/" + String(_bedTarget, 0) + "C\n";
  msg += "Z: " + String(_zHeight, 1) + "mm";
  notificationsservice.sendMSG("ESP3D", msg.c_str());
}

void TelegramMonitor::sendIdleStatus() {
  if (!notificationsservice.started()) return;
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
  String msg = "SD Card Files (" + String(_sdFileCount) + "):\n\n";
  for (uint8_t i = 0; i < _sdFileCount; i++) {
    msg += String(i + 1) + ". " + _sdFiles[i] + "\n";
  }
  msg += "\nTo print, type:\n/print <number> (e.g. /print 1)\nor /print <filename>";
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

  // ── Status & Monitoring ──
  if (lc == "/status") {
    if (_isPrinting || _isPaused) {
      sendStatusUpdate();
    } else {
      sendIdleStatus();
    }
  }
  // ── Temperature ──
  else if (lc == "/temp") {
    String msg = "Temperatures:\n";
    msg += "Hotend: " + String(_hotendActual, 1) + " / " + String(_hotendTarget, 0) + "C\n";
    msg += "Bed: " + String(_bedActual, 1) + " / " + String(_bedTarget, 0) + "C";
    sendTelegramReply(msg);
  }
  // ── Position ──
  else if (lc == "/pos" || lc == "/position") {
    esp3d_serial_service.writeBytes((const uint8_t*)"M114\n", 5);
    String msg = "Tool Head Position:\n";
    msg += "X: " + String(_posX, 2) + " mm\n";
    msg += "Y: " + String(_posY, 2) + " mm\n";
    msg += "Z: " + String(_zHeight, 2) + " mm\n";
    msg += "E: " + String(_posE, 2) + " mm";
    sendTelegramReply(msg);
  }
  // ── Display / LCD info ──
  else if (lc == "/display" || lc == "/screen" || lc == "/lcd") {
    String state = "Idle";
    if (_isPaused) state = "PAUSED";
    else if (_isPrinting) state = "Printing";
    String msg = "Printer Screen:\n";
    msg += "State: " + state + "\n";
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
  }
  // ── Home ──
  else if (lc == "/home") {
    esp3d_serial_service.writeBytes((const uint8_t*)"G28\n", 4);
    sendTelegramReply("Homing all axes (G28 sent)");
  } else if (lc == "/home x") {
    esp3d_serial_service.writeBytes((const uint8_t*)"G28 X\n", 6);
    sendTelegramReply("Homing X axis");
  } else if (lc == "/home y") {
    esp3d_serial_service.writeBytes((const uint8_t*)"G28 Y\n", 6);
    sendTelegramReply("Homing Y axis");
  } else if (lc == "/home z") {
    esp3d_serial_service.writeBytes((const uint8_t*)"G28 Z\n", 6);
    sendTelegramReply("Homing Z axis");
  }
  // ── BUG FIX #4: Pause (M25) ──
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
    sendTelegramReply("Print Paused (M25 sent)\nNozzle will park safely.\nSend /resume to continue or /stop to cancel.");
  }
  // ── BUG FIX #4: Resume (M108 breaks wait_for_user, triggers auto-resume) ──
  else if (lc == "/resume") {
    if (!_isPaused) {
      sendTelegramReply("Printer is not paused. Nothing to resume.");
      return;
    }
    // M108 breaks Marlin's blocking wait_for_user loop via EMERGENCY_PARSER
    // Marlin then automatically calls resume_print() (reheat, unpark, unretract, resume SD)
    // M24 sent as fallback safety net
    esp3d_serial_service.writeBytes((const uint8_t*)"M108\n", 5);
    delay(100);
    esp3d_serial_service.writeBytes((const uint8_t*)"M24\n", 4);
    _isPaused = false;
    _isPrinting = true;
    sendTelegramReply("Print Resuming (M108+M24 sent)\nReheating, unparking, and continuing...");
  }
  // ── BUG FIX #4: Stop (M108 breaks wait, M524 aborts, cool down) ──
  else if (lc == "/stop") {
    if (!_isPrinting && !_isPaused) {
      sendTelegramReply("Nothing to stop - printer is idle.");
      return;
    }
    // M108 breaks wait_for_user if paused
    // M524 aborts SD print
    // M104 S0 / M140 S0 cool down heaters
    esp3d_serial_service.writeBytes((const uint8_t*)"M108\n", 5);
    delay(50);
    esp3d_serial_service.writeBytes((const uint8_t*)"M524\n", 5);
    delay(50);
    esp3d_serial_service.writeBytes((const uint8_t*)"M104 S0\nM140 S0\n", 16);
    _isPaused = false;
    _isPrinting = false;
    _wasPrinting = false;
    _progress = 0;
    sendTelegramReply("Print Cancelled (M108+M524 sent)\nHeaters turning off.\nPrinter will return to idle.");
  }
  // ── Emergency ──
  else if (lc == "/emergency") {
    // M112 is handled by EMERGENCY_PARSER even during wait_for_user
    esp3d_serial_service.writeBytes((const uint8_t*)"M112\n", 5);
    _isPaused = false;
    _isPrinting = false;
    _wasPrinting = false;
    sendTelegramReply("EMERGENCY STOP (M112 sent)\nAll motors and heaters killed immediately!");
  }
  // ── Cooldown ──
  else if (lc == "/cooldown") {
    esp3d_serial_service.writeBytes((const uint8_t*)"M104 S0\nM140 S0\n", 16);
    sendTelegramReply("Cooldown: Hotend and Bed heaters turned OFF");
  }
  // ── List SD files ──
  else if (lc == "/files" || lc == "/ls") {
    _collectingFiles = false;
    _sdFileCount = 0;
    _pendingFilesRequest = true;
    _readyToSendFileList = false;
    _filesRequestTime = millis();
    esp3d_serial_service.writeBytes((const uint8_t*)"M21\nM20\n", 8);
  }
  // ── Print a file ──
  else if (lc.startsWith("/print")) {
    String arg = cmd.substring(6);
    arg.trim();
    if (arg.length() == 0) {
      sendTelegramReply("Usage: /print 1 or /print <filename>\nType /files to see list.");
      return;
    }
    if (_isPrinting || _isPaused) {
      sendTelegramReply("Printer is busy! Stop the current print first with /stop.");
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
  // ── Help ──
  else if (lc == "/help" || lc == "/start") {
    String msg = "3D Printer Remote Control\n\n";
    msg += "/status - Print progress and stats\n";
    msg += "/temp - Temperatures\n";
    msg += "/pos - Tool head position\n";
    msg += "/display - Full printer screen info\n";
    msg += "/home - Home all axes\n";
    msg += "/files - List SD card files\n";
    msg += "/print <n> - Print file by number\n";
    msg += "/pause - Pause print\n";
    msg += "/resume - Resume print\n";
    msg += "/stop - Cancel print safely\n";
    msg += "/emergency - Kill stop\n";
    msg += "/cooldown - Turn off heaters\n";
    msg += "/help - This list";
    sendTelegramReply(msg);
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
  client.stop();  // Close connection BEFORE processing

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
        // BUG FIX #1: On first poll after boot, skip executing commands
        // Only update _lastUpdateId to discard stale messages
        if (!_skipFirstPoll) {
          _pendingCommand = cmdText;
          _pendingFromId = fromId;
        }
      }
    }
    idx = next;
  }

  // After first successful poll, clear skip flag
  if (_skipFirstPoll) _skipFirstPoll = false;
}

// ── Main loop handler ────────────────────────────────────
void TelegramMonitor::handle() {
  if (!_started) return;
  uint32_t now = millis();

  // 1. Execute queued command (safe: SSL connection already closed)
  if (_pendingCommand.length() > 0) {
    String cmd = _pendingCommand;
    String fid = _pendingFromId;
    _pendingCommand = "";
    _pendingFromId = "";
    processTelegramCommand(cmd, fid);
    return;  // Process only one command per handle() cycle for stability
  }

  // 2. Send file list reply (deferred from serial ISR)
  if (_pendingFilesRequest && _readyToSendFileList) {
    _pendingFilesRequest = false;
    _readyToSendFileList = false;
    _collectingFiles = false;
    sendSDFileListReply();
    return;  // One network operation per cycle
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
  } else {
    if (now - _lastPollTime >= 30000) {
      _lastPollTime = now;
      esp3d_serial_service.writeBytes((const uint8_t*)"M105\n", 5);
    }
  }

  // 6. Detect print start
  if (_isPrinting && !_wasPrinting && !_isPaused) {
    sendPrintStarted();
    _lastPrintUpdateTime = now;
    _wasPrinting = true;
    esp3d_serial_service.writeBytes((const uint8_t*)"M27 C\n", 6);
    esp3d_serial_service.writeBytes((const uint8_t*)"M31\n", 4);
  }

  // 7. Detect print finished (BUG FIX #3: only when NOT paused)
  if (!_isPrinting && !_isPaused && _wasPrinting) {
    sendPrintFinished();
    _wasPrinting = false;
    _progress = 0;
    _printTime = "";
    _fileName = "";
    _lastIdleUpdateTime = now;
  }

  // 8. Periodic print update (every 1 minute)
  if ((_isPrinting || _isPaused) && (now - _lastPrintUpdateTime >= TELEGRAM_PRINT_INTERVAL)) {
    sendStatusUpdate();
    _lastPrintUpdateTime = now;
  }

  // 9. Periodic idle update (every 5 minutes)
  if (!_isPrinting && !_isPaused && (now - _lastIdleUpdateTime >= TELEGRAM_IDLE_INTERVAL)) {
    sendIdleStatus();
    _lastIdleUpdateTime = now;
  }
}

#endif  // NOTIFICATION_FEATURE
