/*
  telegram_monitor.cpp - 2-Way Interactive Telegram Monitor & Remote Control

  Features:
  - 2-Way Telegram Bot:
      /status     - Live print progress & stats
      /pause      - Pause print (M25)
      /resume     - Resume print (M24)
      /stop       - Cancel print (M524)
      /emergency  - Emergency kill stop (M112)
      /temp       - Check hotend & bed temperatures
      /cooldown   - Turn off all heaters
      /pos        - Tool head position (X, Y, Z, E)
      /home       - Home axes (G28)
      /display    - Printer LCD screen info & speeds
      /files      - List SD card files with numbers
      /print <n>  - Select file & start printing
      /help       - Show all available commands
  - Periodic print updates every 1 minute
  - Periodic idle updates every 5 minutes
  - Instant print start & finish alerts
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
  _fileName = "Unknown";
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
  _lcdMessage = "Ready";
  _printTime = "0s";
  _sdFileCount = 0;
  _collectingFiles = false;
  _pendingFilesRequest = false;
  _readyToSendFileList = false;
}

void TelegramMonitor::begin() {
  _started = true;
  uint32_t now = millis();
  _lastPrintUpdateTime = now;
  _lastIdleUpdateTime = now;
  _lastPollTime = now;
  _lastTelegramPollTime = now;
  _filesRequestTime = 0;
  _lastUpdateId = 0;
  _pendingCommand = "";
  _pendingFromId = "";
  _sdFileCount = 0;
  _collectingFiles = false;
  _pendingFilesRequest = false;
  _readyToSendFileList = false;
}

// ── Time helper functions ────────────────────────────────
static uint32_t parseElapsedSeconds(const String& timeStr) {
  uint32_t totalSec = 0;
  int hIdx = timeStr.indexOf('h');
  int mIdx = timeStr.indexOf('m');
  int sIdx = timeStr.indexOf('s');
  
  if (hIdx >= 0) {
    int start = 0;
    while (start < hIdx && !isDigit(timeStr.charAt(start))) start++;
    totalSec += timeStr.substring(start, hIdx).toInt() * 3600;
  }
  if (mIdx >= 0) {
    int start = (hIdx >= 0) ? (hIdx + 1) : 0;
    while (start < mIdx && !isDigit(timeStr.charAt(start))) start++;
    totalSec += timeStr.substring(start, mIdx).toInt() * 60;
  }
  if (sIdx >= 0) {
    int start = (mIdx >= 0) ? (mIdx + 1) : (hIdx >= 0 ? (hIdx + 1) : 0);
    while (start < sIdx && !isDigit(timeStr.charAt(start))) start++;
    totalSec += timeStr.substring(start, sIdx).toInt();
  }
  return totalSec;
}

static String formatTime(uint32_t sec) {
  if (sec == 0) return "Almost done";
  uint32_t h = sec / 3600;
  uint32_t m = (sec % 3600) / 60;
  uint32_t s = sec % 60;
  String res = "";
  if (h > 0) {
    res += String(h) + "h " + String(m) + "m";
  } else if (m > 0) {
    res += String(m) + "m " + String(s) + "s";
  } else {
    res += String(s) + "s";
  }
  return res;
}

// ── Split serial chunk into lines and parse each line ───
void TelegramMonitor::parseSerialData(const uint8_t* data, size_t size) {
  if (!_started || !data || size == 0) return;

  const char* p = (const char*)data;
  const char* end = p + size;
  while (p < end) {
    const char* start = p;
    while (p < end && *p != '\n' && *p != '\r') {
      p++;
    }
    size_t len = p - start;
    if (len > 0 && len < 256) {
      char lineBuf[256];
      memcpy(lineBuf, start, len);
      lineBuf[len] = '\0';
      parseSingleLine(lineBuf);
    }
    while (p < end && (*p == '\r' || *p == '\n')) {
      p++;
    }
  }
}

// ── Parse a single line from Marlin ──────────────────────
void TelegramMonitor::parseSingleLine(const char* line) {
  if (!line || strlen(line) == 0) return;

  String data = String(line);
  data.trim();

  // 1. Parse temperature: "T:200.00 /200.00 B:50.00 /50.00"
  int tPos = data.indexOf("T:");
  if (tPos >= 0) {
    int slashPos = data.indexOf('/', tPos);
    if (slashPos > tPos) {
      _hotendActual = data.substring(tPos + 2, slashPos).toFloat();
      int endPos = slashPos + 1;
      while (endPos < (int)data.length() && data.charAt(endPos) != ' ' &&
             data.charAt(endPos) != 'B' && data.charAt(endPos) != '@') {
        endPos++;
      }
      _hotendTarget = data.substring(slashPos + 1, endPos).toFloat();
    }
  }

  int bPos = data.indexOf("B:");
  if (bPos >= 0 && bPos != (tPos + 2)) {
    int slashPos = data.indexOf('/', bPos);
    if (slashPos > bPos) {
      _bedActual = data.substring(bPos + 2, slashPos).toFloat();
      int endPos = slashPos + 1;
      while (endPos < (int)data.length() && data.charAt(endPos) != ' ' &&
             data.charAt(endPos) != '@') {
        endPos++;
      }
      _bedTarget = data.substring(slashPos + 1, endPos).toFloat();
    }
  }

  // 2. Parse positions: "X:0.00 Y:235.00 Z:50.20 E:337.83"
  int xPos = data.indexOf("X:");
  if (xPos >= 0) {
    int endPos = data.indexOf(' ', xPos + 2);
    if (endPos > xPos + 2) {
      _posX = data.substring(xPos + 2, endPos).toFloat();
    }
  }
  int yPos = data.indexOf("Y:");
  if (yPos >= 0) {
    int endPos = data.indexOf(' ', yPos + 2);
    if (endPos > yPos + 2) {
      _posY = data.substring(yPos + 2, endPos).toFloat();
    }
  }
  int zPos = data.indexOf("Z:");
  if (zPos >= 0) {
    int endPos = data.indexOf(' ', zPos + 2);
    if (endPos < 0) endPos = data.length();
    _zHeight = data.substring(zPos + 2, endPos).toFloat();
  }
  int ePos = data.indexOf("E:");
  if (ePos >= 0) {
    int endPos = data.indexOf(' ', ePos + 2);
    if (endPos < 0) endPos = data.length();
    _posE = data.substring(ePos + 2, endPos).toFloat();
  }

  // 3. Parse filename
  int cfPos = data.indexOf("Current file: ");
  if (cfPos >= 0) {
    _fileName = data.substring(cfPos + 14);
    _fileName.trim();
  }
  int foPos = data.indexOf("File opened: ");
  if (foPos >= 0) {
    int spPos = data.indexOf(' ', foPos + 13);
    if (spPos < 0) spPos = data.length();
    _fileName = data.substring(foPos + 13, spPos);
    _fileName.trim();
  }

  // 4. Parse SD progress: "SD printing byte 818544/2344893"
  int sdPos = data.indexOf("SD printing byte ");
  if (sdPos >= 0) {
    int numStart = sdPos + 17;
    int slashPos = data.indexOf('/', numStart);
    if (slashPos > numStart) {
      _bytesDone = data.substring(numStart, slashPos).toInt();
      int endPos = slashPos + 1;
      while (endPos < (int)data.length() && isDigit(data.charAt(endPos))) {
        endPos++;
      }
      _bytesTotal = data.substring(slashPos + 1, endPos).toInt();
      if (_bytesTotal > 0) {
        _progress = ((float)_bytesDone / (float)_bytesTotal) * 100.0;
        _isPrinting = true;
      }
    }
  }

  if (data.indexOf("Not SD printing") >= 0) {
    _isPrinting = false;
  }

  // 5. Parse print time: "echo:Print time: 53m 52s"
  int ptPos = data.indexOf("Print time: ");
  if (ptPos >= 0) {
    _printTime = data.substring(ptPos + 12);
    _printTime.trim();
    if (_printTime.length() > 20) {
      _printTime = _printTime.substring(0, 20);
    }
  }

  // 6. Parse Speed / Feedrate & Flowrate: "FR:100%" "E0 Flow: 100%"
  int frPos = data.indexOf("FR:");
  if (frPos >= 0) {
    int pctPos = data.indexOf('%', frPos);
    if (pctPos > frPos + 3) {
      _feedrate = data.substring(frPos + 3, pctPos).toInt();
    }
  }
  int flPos = data.indexOf("Flow: ");
  if (flPos < 0) flPos = data.indexOf("flow: ");
  if (flPos >= 0) {
    int pctPos = data.indexOf('%', flPos);
    if (pctPos > flPos + 6) {
      _flowrate = data.substring(flPos + 6, pctPos).toInt();
    }
  }

  // 7. Parse LCD messages: "echo:M117 Message" or "//action:notification Message"
  int m117Pos = data.indexOf("M117 ");
  if (m117Pos >= 0) {
    _lcdMessage = data.substring(m117Pos + 5);
    _lcdMessage.trim();
  }
  int notifPos = data.indexOf("//action:notification ");
  if (notifPos >= 0) {
    _lcdMessage = data.substring(notifPos + 22);
    _lcdMessage.trim();
  }

  // 8. Parse SD file list (from M20):
  if (data.indexOf("Begin file list") >= 0) {
    _collectingFiles = true;
    _sdFileCount = 0;
    return;
  }
  if (data.indexOf("End file list") >= 0) {
    _collectingFiles = false;
    if (_pendingFilesRequest) {
      _readyToSendFileList = true;
    }
    return;
  }
  if (_collectingFiles) {
    String fname = data;
    fname.trim();
    if (fname.length() > 0 && !fname.startsWith("ok") && !fname.startsWith("//") && !fname.startsWith("echo:")) {
      int spaceIdx = fname.indexOf(' ');
      if (spaceIdx > 0) {
        fname = fname.substring(0, spaceIdx);
      }
      if (_sdFileCount < MAX_SD_FILES) {
        _sdFiles[_sdFileCount++] = fname;
      }
    }
  }
}

// ── Build progress bar ───────────────────────────────────
static String buildProgressBar(float pct) {
  int filled = (int)(pct / 100.0 * 10);
  if (filled < 0) filled = 0;
  if (filled > 10) filled = 10;
  String bar = "[";
  for (int i = 0; i < 10; i++) {
    bar += (i < filled) ? "#" : "-";
  }
  bar += "]";
  return bar;
}

// ── Send periodic status update while printing ───────────
void TelegramMonitor::sendStatusUpdate() {
  if (!notificationsservice.started()) return;

  uint32_t elapsedSec = parseElapsedSeconds(_printTime);
  String timeLeftStr = "Calculating...";
  if (elapsedSec > 10 && _progress > 1.0 && _progress < 99.5) {
    float totalSec = (float)elapsedSec / (_progress / 100.0);
    uint32_t remSec = (uint32_t)(totalSec - elapsedSec);
    timeLeftStr = formatTime(remSec);
  }

  String msg = "Print Status\n";
  if (_fileName != "Unknown" && _fileName.length() > 0) {
    msg += "File: " + _fileName + "\n";
  }
  msg += buildProgressBar(_progress) + " " + String(_progress, 1) + "%\n";
  msg += "Elapsed: " + _printTime + "\n";
  msg += "Time Left: ~" + timeLeftStr + "\n";
  msg += "Hotend: " + String(_hotendActual, 0) + "/" + String(_hotendTarget, 0) + "C\n";
  msg += "Bed: " + String(_bedActual, 0) + "/" + String(_bedTarget, 0) + "C\n";
  msg += "Z: " + String(_zHeight, 1) + "mm";

  notificationsservice.sendMSG("ESP3D", msg.c_str());
}

// ── Send periodic idle status ────────────────────────────
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
  if (_fileName != "Unknown" && _fileName.length() > 0) {
    msg += "File: " + _fileName + "\n";
  }
  msg += "Hotend: " + String(_hotendTarget, 0) + "C | Bed: " + String(_bedTarget, 0) + "C";

  notificationsservice.sendMSG("ESP3D", msg.c_str());
}

void TelegramMonitor::sendPrintFinished() {
  if (!notificationsservice.started()) return;
  
  String msg = "Print Finished!\n";
  if (_fileName != "Unknown" && _fileName.length() > 0) {
    msg += "File: " + _fileName + "\n";
  }
  msg += "Total Time: " + _printTime + "\n";
  msg += "Hotend: " + String(_hotendActual, 0) + "C | Bed: " + String(_bedActual, 0) + "C";

  notificationsservice.sendMSG("ESP3D", msg.c_str());
}

void TelegramMonitor::sendTelegramReply(const String& text) {
  notificationsservice.sendMSG("ESP3D", text.c_str());
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
  msg += "\nTo start printing, type:\n/print <number> (e.g. /print 1)\nor /print <filename>";
  sendTelegramReply(msg);
}

// ── 2-Way Telegram Bot Command Processing ─────────────────
void TelegramMonitor::processTelegramCommand(String cmd, const String& fromId) {
  const String& authChatId = notificationsservice.getToken2();
  
  // Security check: only process commands from authorized chat_id
  if (authChatId.length() > 0 && fromId.length() > 0 && fromId != authChatId) {
    return;
  }

  cmd.trim();
  int atIdx = cmd.indexOf('@');
  if (atIdx >= 0) {
    cmd = cmd.substring(0, atIdx);
  }

  String lowerCmd = cmd;
  lowerCmd.toLowerCase();

  if (lowerCmd == "/status") {
    if (_isPrinting) {
      sendStatusUpdate();
    } else {
      sendIdleStatus();
    }
  } else if (lowerCmd == "/pause") {
    esp3d_serial_service.writeBytes((const uint8_t*)"M25\n", 4);
    sendTelegramReply("Print Paused (M25 sent)");
  } else if (lowerCmd == "/resume") {
    esp3d_serial_service.writeBytes((const uint8_t*)"M24\n", 4);
    sendTelegramReply("Print Resumed (M24 sent)");
  } else if (lowerCmd == "/stop") {
    esp3d_serial_service.writeBytes((const uint8_t*)"M524\n", 5);
    sendTelegramReply("Print Cancelled (M524 sent)");
  } else if (lowerCmd == "/emergency") {
    esp3d_serial_service.writeBytes((const uint8_t*)"M112\n", 5);
    sendTelegramReply("EMERGENCY STOP! M112 issued. Motors and heaters shut down immediately!");
  } else if (lowerCmd == "/temp") {
    String msg = "Temperatures:\n";
    msg += "Hotend: " + String(_hotendActual, 1) + " / " + String(_hotendTarget, 0) + "C\n";
    msg += "Bed: " + String(_bedActual, 1) + " / " + String(_bedTarget, 0) + "C";
    sendTelegramReply(msg);
  } else if (lowerCmd == "/cooldown") {
    esp3d_serial_service.writeBytes((const uint8_t*)"M104 S0\nM140 S0\n", 16);
    sendTelegramReply("Cooldown: Hotend and Bed heaters turned OFF");
  } else if (lowerCmd == "/pos" || lowerCmd == "/position") {
    esp3d_serial_service.writeBytes((const uint8_t*)"M114\n", 5);
    String msg = "Tool Head Position:\n";
    msg += "X: " + String(_posX, 2) + " mm\n";
    msg += "Y: " + String(_posY, 2) + " mm\n";
    msg += "Z: " + String(_zHeight, 2) + " mm\n";
    msg += "E: " + String(_posE, 2) + " mm";
    sendTelegramReply(msg);
  } else if (lowerCmd == "/home") {
    esp3d_serial_service.writeBytes((const uint8_t*)"G28\n", 4);
    sendTelegramReply("Homing all axes (G28 sent)");
  } else if (lowerCmd == "/home x") {
    esp3d_serial_service.writeBytes((const uint8_t*)"G28 X\n", 6);
    sendTelegramReply("Homing X axis (G28 X sent)");
  } else if (lowerCmd == "/home y") {
    esp3d_serial_service.writeBytes((const uint8_t*)"G28 Y\n", 6);
    sendTelegramReply("Homing Y axis (G28 Y sent)");
  } else if (lowerCmd == "/home z") {
    esp3d_serial_service.writeBytes((const uint8_t*)"G28 Z\n", 6);
    sendTelegramReply("Homing Z axis (G28 Z sent)");
  } else if (lowerCmd == "/display" || lowerCmd == "/screen" || lowerCmd == "/lcd") {
    String msg = "Printer Screen / Status:\n";
    msg += "State: " + String(_isPrinting ? "Printing" : "Idle") + "\n";
    if (_isPrinting) {
      msg += "File: " + _fileName + "\n";
      msg += "Progress: " + String(_progress, 1) + "%\n";
      msg += "Elapsed: " + _printTime + "\n";
    }
    msg += "Hotend: " + String(_hotendActual, 0) + "/" + String(_hotendTarget, 0) + "C\n";
    msg += "Bed: " + String(_bedActual, 0) + "/" + String(_bedTarget, 0) + "C\n";
    msg += "Z: " + String(_zHeight, 2) + " mm\n";
    msg += "Speed: " + String(_feedrate) + "% | Flow: " + String(_flowrate) + "%\n";
    if (_lcdMessage.length() > 0 && _lcdMessage != "Ready") {
      msg += "Message: " + _lcdMessage;
    }
    sendTelegramReply(msg);
  } else if (lowerCmd == "/files" || lowerCmd == "/ls") {
    _collectingFiles = false;
    _sdFileCount = 0;
    _pendingFilesRequest = true;
    _filesRequestTime = millis();
    esp3d_serial_service.writeBytes((const uint8_t*)"M21\nM20\n", 8);
  } else if (lowerCmd.startsWith("/print")) {
    String arg = cmd.substring(6);
    arg.trim();
    if (arg.length() == 0) {
      sendTelegramReply("Please specify a file number or name.\nUsage: /print 1 or /print <filename>\nType /files to see list.");
      return;
    }

    String selectedFile = "";
    bool isNum = true;
    for (size_t i = 0; i < arg.length(); i++) {
      if (!isDigit(arg.charAt(i))) { isNum = false; break; }
    }

    if (isNum) {
      int fileIdx = arg.toInt() - 1;
      if (fileIdx >= 0 && fileIdx < _sdFileCount) {
        selectedFile = _sdFiles[fileIdx];
      } else {
        sendTelegramReply("Invalid file number. Type /files to see available numbers.");
        return;
      }
    } else {
      selectedFile = arg;
      selectedFile.toUpperCase();
    }

    String m23 = "M23 " + selectedFile + "\n";
    esp3d_serial_service.writeBytes((const uint8_t*)m23.c_str(), m23.length());
    esp3d_serial_service.writeBytes((const uint8_t*)"M24\n", 4);

    _fileName = selectedFile;
    sendTelegramReply("Starting print for: " + selectedFile + "\nM23 and M24 sent!");
  } else if (lowerCmd == "/help" || lowerCmd == "/start") {
    String msg = "3D Printer Remote Control\n\n";
    msg += "/status - Live print progress and stats\n";
    msg += "/pos - Tool head position (X, Y, Z, E)\n";
    msg += "/home - Home all axes (G28)\n";
    msg += "/display - Screen info, speeds and message\n";
    msg += "/files - List SD card files\n";
    msg += "/print <num|file> - Select and print file\n";
    msg += "/pause - Pause print (M25)\n";
    msg += "/resume - Resume print (M24)\n";
    msg += "/stop - Cancel print (M524)\n";
    msg += "/emergency - Kill stop (M112)\n";
    msg += "/temp - Check temperatures\n";
    msg += "/cooldown - Turn off heaters\n";
    msg += "/help - Show command list";
    sendTelegramReply(msg);
  } else if (lowerCmd.startsWith("/")) {
    sendTelegramReply("Unknown command. Type /help to see all available commands.");
  }
}

// ── Poll Telegram getUpdates for incoming messages ───────
void TelegramMonitor::pollTelegramUpdates() {
  if (!notificationsservice.started()) return;
  if (notificationsservice.getNotificationType() != ESP_TELEGRAM_NOTIFICATION) return;

  const String& token = notificationsservice.getToken1();
  const String& authChatId = notificationsservice.getToken2();
  if (token.length() == 0 || authChatId.length() == 0) return;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(2);

  if (!client.connect("api.telegram.org", 443)) {
    return;
  }

  String body = "offset=" + String((long long)(_lastUpdateId + 1)) + "&limit=3&timeout=0";

  String req = "POST /bot" + token + "/getUpdates HTTP/1.1\r\n";
  req += "Host: api.telegram.org\r\n";
  req += "Connection: close\r\n";
  req += "Content-Type: application/x-www-form-urlencoded\r\n";
  req += "Content-Length: " + String(body.length()) + "\r\n\r\n";
  req += body;

  client.print(req);

  uint32_t startMs = millis();
  while (!client.available() && (millis() - startMs < 2000)) {
    delay(10);
  }

  if (!client.available()) {
    client.stop();
    return;
  }

  bool inBody = false;
  String line = "";
  while (client.connected() || client.available()) {
    char c = client.read();
    if (c == (char)-1) continue;
    line += c;
    if (!inBody) {
      if (line.endsWith("\r\n\r\n")) {
        inBody = true;
        line = "";
      }
    } else {
      if (line.length() > 4096) break;
    }
  }
  // Stop and disconnect client immediately before parsing and queueing commands!
  client.stop();

  if (!inBody || line.length() == 0) return;

  // Parse updates in JSON
  int idx = 0;
  while ((idx = line.indexOf("\"update_id\":", idx)) >= 0) {
    int idStart = idx + 12;
    int idEnd = idStart;
    while (idEnd < (int)line.length() && (isDigit(line.charAt(idEnd)) || line.charAt(idEnd) == '-')) {
      idEnd++;
    }
    int64_t updateId = atoll(line.substring(idStart, idEnd).c_str());
    if (updateId > _lastUpdateId) {
      _lastUpdateId = updateId;
    }

    int nextUpdate = line.indexOf("\"update_id\":", idEnd);
    if (nextUpdate < 0) nextUpdate = line.length();

    String updateChunk = line.substring(idEnd, nextUpdate);

    // Extract sender / chat ID
    String fromId = "";
    int chatPos = updateChunk.indexOf("\"chat\":{");
    if (chatPos >= 0) {
      int idPos = updateChunk.indexOf("\"id\":", chatPos);
      if (idPos >= 0) {
        int s = idPos + 5;
        int e = s;
        while (e < (int)updateChunk.length() && (isDigit(updateChunk.charAt(e)) || updateChunk.charAt(e) == '-')) {
          e++;
        }
        fromId = updateChunk.substring(s, e);
      }
    }

    // Extract message text
    int textPos = updateChunk.indexOf("\"text\":\"", 0);
    if (textPos >= 0) {
      int tStart = textPos + 8;
      int tEnd = updateChunk.indexOf('"', tStart);
      if (tEnd > tStart) {
        String cmdText = updateChunk.substring(tStart, tEnd);
        // Queue command to execute cleanly outside the polling connection
        _pendingCommand = cmdText;
        _pendingFromId = fromId;
      }
    }

    idx = nextUpdate;
  }
}

// ── Main handle - called from ESP3D main loop ────────────
void TelegramMonitor::handle() {
  if (!_started) return;

  uint32_t now = millis();

  // 1. Execute any queued command (after polling SSL connection is 100% closed)
  if (_pendingCommand.length() > 0) {
    String cmd = _pendingCommand;
    String fromId = _pendingFromId;
    _pendingCommand = "";
    _pendingFromId = "";
    processTelegramCommand(cmd, fromId);
  }

  // 2. Check for incoming Telegram commands every 4 seconds
  if (now - _lastTelegramPollTime >= TELEGRAM_POLL_INTERVAL) {
    _lastTelegramPollTime = now;
    pollTelegramUpdates();
  }

  // 3. Handle files request completion (when End file list received or on 4s timeout)
  if (_pendingFilesRequest && (_readyToSendFileList || (now - _filesRequestTime >= 4000))) {
    _pendingFilesRequest = false;
    _readyToSendFileList = false;
    _collectingFiles = false;
    sendSDFileListReply();
  }

  // 4. Periodic polling to Marlin for filename, print time, pos, and temps
  if (_isPrinting) {
    if (now - _lastPollTime >= 12000) {
      _lastPollTime = now;
      if (_fileName == "Unknown" || _fileName.length() == 0) {
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

  // 5. Detect print started
  if (_isPrinting && !_wasPrinting) {
    sendPrintStarted();
    _lastPrintUpdateTime = now;
    _wasPrinting = true;
    esp3d_serial_service.writeBytes((const uint8_t*)"M27 C\n", 6);
    esp3d_serial_service.writeBytes((const uint8_t*)"M31\n", 4);
  }

  // 6. Detect print finished
  if (!_isPrinting && _wasPrinting) {
    sendPrintFinished();
    _wasPrinting = false;
    _progress = 0;
    _printTime = "0s";
    _fileName = "Unknown";
    _lastIdleUpdateTime = now;
  }

  // 7. Periodic update while printing (every 1 minute)
  if (_isPrinting && (now - _lastPrintUpdateTime >= TELEGRAM_PRINT_INTERVAL)) {
    sendStatusUpdate();
    _lastPrintUpdateTime = now;
  }

  // 8. Periodic update while idle (every 5 minutes)
  if (!_isPrinting && (now - _lastIdleUpdateTime >= TELEGRAM_IDLE_INTERVAL)) {
    sendIdleStatus();
    _lastIdleUpdateTime = now;
  }
}

#endif  // NOTIFICATION_FEATURE
