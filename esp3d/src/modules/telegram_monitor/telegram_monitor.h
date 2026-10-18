/*
  telegram_monitor.h - 2-Way Interactive Telegram Monitor & Remote Control

  Features:
  - 2-Way Telegram Bot: /status, /pause, /resume, /stop, /emergency, /temp, /cooldown, /help
  - Position & Movement: /pos (tool coordinates), /home (home axes)
  - Screen Info: /display (LCD status, speeds, message)
  - SD File Management: /files (list files), /print <num|name> (choose and print)
  - 1-minute updates while printing (file name, elapsed time, estimated time left, progress, Z, temps)
  - 5-minute idle status updates
  - Instant print start & finish alerts
*/

#ifndef _TELEGRAM_MONITOR_H
#define _TELEGRAM_MONITOR_H

#include <Arduino.h>

// ── Configuration ────────────────────────────────────────
#define TELEGRAM_PRINT_INTERVAL  60000   // 1 minute while printing (60,000 ms)
#define TELEGRAM_IDLE_INTERVAL   300000  // 5 minutes when idle (300,000 ms)
#define TELEGRAM_POLL_INTERVAL   4000    // Check incoming Telegram commands every 4s
#define MAX_SD_FILES             25      // Maximum number of SD files to cache

class TelegramMonitor {
 public:
  TelegramMonitor();
  void begin();
  void handle();  // Call from main loop
  
  // Called by serial service when data arrives from Marlin
  void parseSerialData(const uint8_t* data, size_t size);

 private:
  bool _started;
  uint32_t _lastPrintUpdateTime;
  uint32_t _lastIdleUpdateTime;
  uint32_t _lastPollTime;
  uint32_t _lastTelegramPollTime;
  uint32_t _filesRequestTime;
  int64_t _lastUpdateId;
  
  // Decoupled pending command queue
  String _pendingCommand;
  String _pendingFromId;

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
  float _posX;
  float _posY;
  float _zHeight;
  float _posE;
  int _feedrate;
  int _flowrate;
  String _lcdMessage;
  String _printTime;
  
  // SD file listing
  String _sdFiles[MAX_SD_FILES];
  uint8_t _sdFileCount;
  bool _collectingFiles;
  bool _pendingFilesRequest;
  bool _readyToSendFileList;

  void parseSingleLine(const char* line);
  void sendStatusUpdate();
  void sendIdleStatus();
  void sendPrintStarted();
  void sendPrintFinished();
  void sendSDFileListReply();
  
  // 2-Way Telegram methods
  void pollTelegramUpdates();
  void processTelegramCommand(String cmd, const String& fromId);
  void sendTelegramReply(const String& text);
};

extern TelegramMonitor telegram_monitor;

#endif  // _TELEGRAM_MONITOR_H
