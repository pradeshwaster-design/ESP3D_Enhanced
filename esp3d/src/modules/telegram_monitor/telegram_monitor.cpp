/*
  telegram_monitor.cpp - Periodic Telegram print status monitor

  Parses Marlin auto-report data from the serial stream and sends
  periodic formatted status updates via ESP3D's notification system.
*/

#include "../../include/esp3d_config.h"
#if defined(NOTIFICATION_FEATURE)

#include "telegram_monitor.h"
#include "../notifications/notifications_service.h"

TelegramMonitor telegram_monitor;

TelegramMonitor::TelegramMonitor() {
  _started = false;
  _lastUpdateTime = 0;
  _isPrinting = false;
  _wasPrinting = false;
  _hotendActual = 0;
  _hotendTarget = 0;
  _bedActual = 0;
  _bedTarget = 0;
  _progress = 0;
  _bytesDone = 0;
  _bytesTotal = 0;
  _zHeight = 0;
  _printTime = "N/A";
}

void TelegramMonitor::begin() {
  _started = true;
  _lastUpdateTime = millis();
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
      // Find end of target temp (next space or B: or @)
      int endPos = slashPos + 1;
      while (endPos < (int)data.length() && data.charAt(endPos) != ' ' &&
             data.charAt(endPos) != 'B' && data.charAt(endPos) != '@') {
        endPos++;
      }
      _hotendTarget = data.substring(slashPos + 1, endPos).toFloat();
    }
  }

  int bPos = data.indexOf("B:");
  if (bPos >= 0 && bPos != (tPos + 2)) {  // Make sure it's not part of "TB:"
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

  // Parse SD progress: "SD printing byte 818544/2344893"
  int sdPos = data.indexOf("SD printing byte ");
  if (sdPos >= 0) {
    int numStart = sdPos + 17;  // length of "SD printing byte "
    int slashPos = data.indexOf('/', numStart);
    if (slashPos > numStart) {
      _bytesDone = data.substring(numStart, slashPos).toInt();
      // Find end of total
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
  int ptPos = data.indexOf("Print time:");
  if (ptPos >= 0) {
    _printTime = data.substring(ptPos + 12);
    _printTime.trim();
    // Truncate if too long
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

// ── Send periodic status update ──────────────────────────
void TelegramMonitor::sendStatusUpdate() {
  if (!notificationsservice.started()) return;

  String msg = "Print Status\n";
  msg += buildProgressBar(_progress);
  msg += " ";
  msg += String(_progress, 1);
  msg += "%\n";
  msg += "Hotend: ";
  msg += String(_hotendActual, 0);
  msg += "/";
  msg += String(_hotendTarget, 0);
  msg += "C\n";
  msg += "Bed: ";
  msg += String(_bedActual, 0);
  msg += "/";
  msg += String(_bedTarget, 0);
  msg += "C\n";
  msg += "Z: ";
  msg += String(_zHeight, 1);
  msg += "mm\n";
  msg += "Time: ";
  msg += _printTime;

  notificationsservice.sendMSG("ESP3D", msg.c_str());
}

void TelegramMonitor::sendPrintStarted() {
  if (!notificationsservice.started()) return;
  
  String msg = "Print Started!\n";
  msg += "Hotend: ";
  msg += String(_hotendTarget, 0);
  msg += "C | Bed: ";
  msg += String(_bedTarget, 0);
  msg += "C";

  notificationsservice.sendMSG("ESP3D", msg.c_str());
}

void TelegramMonitor::sendPrintFinished() {
  if (!notificationsservice.started()) return;
  
  String msg = "Print Finished!\n";
  msg += "Total Time: ";
  msg += _printTime;
  msg += "\nHotend: ";
  msg += String(_hotendActual, 0);
  msg += "C | Bed: ";
  msg += String(_bedActual, 0);
  msg += "C";

  notificationsservice.sendMSG("ESP3D", msg.c_str());
}

// ── Main handle - called from ESP3D main loop ────────────
void TelegramMonitor::handle() {
  if (!_started) return;

  uint32_t now = millis();

  // Detect print started
  if (_isPrinting && !_wasPrinting) {
    sendPrintStarted();
    _lastUpdateTime = now;
    _wasPrinting = true;
  }

  // Detect print finished
  if (!_isPrinting && _wasPrinting) {
    sendPrintFinished();
    _wasPrinting = false;
    _progress = 0;
    _printTime = "N/A";
  }

  // Periodic update while printing
  if (_isPrinting && (now - _lastUpdateTime >= TELEGRAM_UPDATE_INTERVAL)) {
    sendStatusUpdate();
    _lastUpdateTime = now;
  }
}

#endif  // NOTIFICATION_FEATURE
