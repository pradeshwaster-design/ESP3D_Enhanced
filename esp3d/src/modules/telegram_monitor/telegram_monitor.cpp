/*
  telegram_monitor.cpp - Periodic Telegram print status monitor

  Parses Marlin auto-report data from the serial stream and sends:
  - Updates every 1 minute while printing (filename, elapsed time, time left, %, Z, temps)
  - Updates every 5 minutes when idle (idle status, temperatures)
  - Instant notifications on print start and print finish
*/

#include "../../include/esp3d_config.h"
#if defined(NOTIFICATION_FEATURE)

#include "telegram_monitor.h"
#include "../notifications/notifications_service.h"
#include "../serial/serial_service.h"

TelegramMonitor telegram_monitor;

TelegramMonitor::TelegramMonitor() {
  _started = false;
  _lastPrintUpdateTime = 0;
  _lastIdleUpdateTime = 0;
  _lastPollTime = 0;
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
  _zHeight = 0;
  _printTime = "0s";
}

void TelegramMonitor::begin() {
  _started = true;
  uint32_t now = millis();
  _lastPrintUpdateTime = now;
  _lastIdleUpdateTime = now;
  _lastPollTime = now;
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

// ── Parse incoming serial lines from Marlin ──────────────
void TelegramMonitor::parseSerialData(const char* line) {
  if (!_started || !line) return;

  String data = String(line);

  // Parse temperature: " T:200.00 /200.00 B:50.00 /50.00"
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

  // Parse filename from: "Current file: filename.gcode"
  int cfPos = data.indexOf("Current file: ");
  if (cfPos >= 0) {
    int endPos = data.indexOf('\r', cfPos);
    if (endPos < 0) endPos = data.indexOf('\n', cfPos);
    if (endPos < 0) endPos = data.length();
    _fileName = data.substring(cfPos + 14, endPos);
    _fileName.trim();
  }

  // Parse filename from: "File opened: filename.gcode Size: ..."
  int foPos = data.indexOf("File opened: ");
  if (foPos >= 0) {
    int spPos = data.indexOf(' ', foPos + 13);
    if (spPos < 0) spPos = data.indexOf('\r', foPos);
    if (spPos < 0) spPos = data.indexOf('\n', foPos);
    if (spPos < 0) spPos = data.length();
    _fileName = data.substring(foPos + 13, spPos);
    _fileName.trim();
  }

  // Parse SD progress: "SD printing byte 818544/2344893"
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

  // Also check "printing <filename> byte <done>/<total>"
  int prPos = data.indexOf("printing ");
  if (prPos >= 0 && sdPos < 0) {
    int bytePos = data.indexOf(" byte ", prPos);
    if (bytePos > prPos + 9) {
      _fileName = data.substring(prPos + 9, bytePos);
      _fileName.trim();
    }
  }

  // Detect "Not SD printing"
  if (data.indexOf("Not SD printing") >= 0) {
    _isPrinting = false;
  }

  // Parse Z position: "Z:10.80"
  int zPos = data.indexOf("Z:");
  if (zPos >= 0) {
    int endPos = zPos + 2;
    while (endPos < (int)data.length() &&
           (isDigit(data.charAt(endPos)) || data.charAt(endPos) == '.')) {
      endPos++;
    }
    if (endPos > zPos + 2) {
      _zHeight = data.substring(zPos + 2, endPos).toFloat();
    }
  }

  // Parse print time: "echo:Print time: 53m 52s"
  int ptPos = data.indexOf("Print time: ");
  if (ptPos >= 0) {
    int endPos = data.indexOf('\r', ptPos);
    if (endPos < 0) endPos = data.indexOf('\n', ptPos);
    if (endPos < 0) endPos = data.length();
    _printTime = data.substring(ptPos + 12, endPos);
    _printTime.trim();
    if (_printTime.length() > 20) {
      _printTime = _printTime.substring(0, 20);
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

  // Calculate estimated time left
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

// ── Main handle - called from ESP3D main loop ────────────
void TelegramMonitor::handle() {
  if (!_started) return;

  uint32_t now = millis();

  // Periodic polling to Marlin for filename, print time, and temps
  if (_isPrinting) {
    if (now - _lastPollTime >= 12000) {  // Every 12 seconds while printing
      _lastPollTime = now;
      if (_fileName == "Unknown" || _fileName.length() == 0) {
        esp3d_serial_service.writeBytes((const uint8_t*)"M27 C\n", 6);
      }
      esp3d_serial_service.writeBytes((const uint8_t*)"M31\n", 4);
    }
  } else {
    if (now - _lastPollTime >= 30000) {  // Every 30 seconds while idle
      _lastPollTime = now;
      esp3d_serial_service.writeBytes((const uint8_t*)"M105\n", 5);
    }
  }

  // Detect print started
  if (_isPrinting && !_wasPrinting) {
    sendPrintStarted();
    _lastPrintUpdateTime = now;
    _wasPrinting = true;
    // Request filename and print time right away
    esp3d_serial_service.writeBytes((const uint8_t*)"M27 C\n", 6);
    esp3d_serial_service.writeBytes((const uint8_t*)"M31\n", 4);
  }

  // Detect print finished
  if (!_isPrinting && _wasPrinting) {
    sendPrintFinished();
    _wasPrinting = false;
    _progress = 0;
    _printTime = "0s";
    _fileName = "Unknown";
    _lastIdleUpdateTime = now;
  }

  // Periodic update while printing (every 1 minute)
  if (_isPrinting && (now - _lastPrintUpdateTime >= TELEGRAM_PRINT_INTERVAL)) {
    sendStatusUpdate();
    _lastPrintUpdateTime = now;
  }

  // Periodic update while idle (every 5 minutes)
  if (!_isPrinting && (now - _lastIdleUpdateTime >= TELEGRAM_IDLE_INTERVAL)) {
    sendIdleStatus();
    _lastIdleUpdateTime = now;
  }
}

#endif  // NOTIFICATION_FEATURE
