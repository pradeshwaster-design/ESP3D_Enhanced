/*
  telegram_monitor.h - Periodic Telegram print status monitor

  Custom module for ESP3D to send periodic print status updates via Telegram.
  Parses Marlin auto-report data (temperatures, SD progress, positions)
  from the serial stream and sends formatted updates at configurable intervals.
*/

#ifndef _TELEGRAM_MONITOR_H
#define _TELEGRAM_MONITOR_H

#include <Arduino.h>

// ── Configuration ────────────────────────────────────────
// How often to send Telegram updates while printing (milliseconds)
#define TELEGRAM_UPDATE_INTERVAL  60000   // 5 minutes (300,000 ms)

class TelegramMonitor {
 public:
  TelegramMonitor();
  void begin();
  void handle();  // Call from main loop
  
  // Called by serial service when data arrives from Marlin
  void parseSerialData(const char* line);

 private:
  bool _started;
  uint32_t _lastUpdateTime;
  
  // Printer state parsed from serial stream
  bool _isPrinting;
  bool _wasPrinting;
  float _hotendActual;
  float _hotendTarget;
  float _bedActual;
  float _bedTarget;
  float _progress;       // 0-100%
  uint32_t _bytesDone;
  uint32_t _bytesTotal;
  float _zHeight;
  String _printTime;
  
  void sendStatusUpdate();
  void sendPrintStarted();
  void sendPrintFinished();
};

extern TelegramMonitor telegram_monitor;

#endif  // _TELEGRAM_MONITOR_H
