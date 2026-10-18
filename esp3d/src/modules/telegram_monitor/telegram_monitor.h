/*
  telegram_monitor.h - Periodic Telegram print status monitor

  Custom module for ESP3D to send periodic print status updates via Telegram.
  - While printing: updates every 1 minute with filename, elapsed time, estimated time left, %, Z, temps.
  - While idle: updates every 5 minutes with printer idle status & temperatures.
*/

#ifndef _TELEGRAM_MONITOR_H
#define _TELEGRAM_MONITOR_H

#include <Arduino.h>

// ── Configuration ────────────────────────────────────────
#define TELEGRAM_PRINT_INTERVAL  60000   // 1 minute while printing (60,000 ms)
#define TELEGRAM_IDLE_INTERVAL   300000  // 5 minutes when idle (300,000 ms)

class TelegramMonitor {
 public:
  TelegramMonitor();
  void begin();
  void handle();  // Call from main loop
  
  // Called by serial service when data arrives from Marlin
  void parseSerialData(const char* line);

 private:
  bool _started;
  uint32_t _lastPrintUpdateTime;
  uint32_t _lastIdleUpdateTime;
  uint32_t _lastPollTime;
  
  // Printer state parsed from serial stream
  bool _isPrinting;
  bool _wasPrinting;
  String _fileName;
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
  void sendIdleStatus();
  void sendPrintStarted();
  void sendPrintFinished();
};

extern TelegramMonitor telegram_monitor;

#endif  // _TELEGRAM_MONITOR_H
