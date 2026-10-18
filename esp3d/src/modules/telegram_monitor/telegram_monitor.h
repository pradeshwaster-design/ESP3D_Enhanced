/*
  telegram_monitor.h - 2-Way Interactive Telegram Monitor & Remote Control
  
  V3 - All bugs fixed including:
  - Auto-break Marlin's multiple wait_for_user loops on resume
  - Track Marlin prompt/LCD state (Purge More / Continue detection)
  - Skip stale Telegram messages on boot (no double reboot)
  - Pause state tracking (no false "Print Finished" on pause)
  - Correct Marlin commands (M108 for resume, M108+M524 for stop)
  - Safe deferred network calls (no crashes from serial ISR)
*/

#ifndef _TELEGRAM_MONITOR_H
#define _TELEGRAM_MONITOR_H

#include <Arduino.h>

// ── Configuration ────────────────────────────────────────
#define TELEGRAM_PRINT_INTERVAL  60000   // 1 minute while printing
#define TELEGRAM_IDLE_INTERVAL   300000  // 5 minutes when idle
#define TELEGRAM_POLL_INTERVAL   4000    // Check Telegram commands every 4s
#define MAX_SD_FILES             25
#define AUTO_M108_COOLDOWN       4000    // Min ms between auto-M108 sends
#define AUTO_M108_MAX_COUNT      5       // Max auto-M108s per resume/stop

class TelegramMonitor {
 public:
  TelegramMonitor();
  void begin();
  void handle();
  void parseSerialData(const uint8_t* data, size_t size);

 private:
  bool _started;
  bool _skipFirstPoll;          // Skip processing commands on first poll after boot
  uint32_t _lastPrintUpdateTime;
  uint32_t _lastIdleUpdateTime;
  uint32_t _lastPollTime;
  uint32_t _lastTelegramPollTime;
  uint32_t _filesRequestTime;
  int64_t _lastUpdateId;

  // Queued command from Telegram (processed in handle(), not in poll)
  String _pendingCommand;
  String _pendingFromId;

  // Printer state
  bool _isPrinting;
  bool _wasPrinting;
  bool _isPaused;               // True when M25 pause is active
  String _fileName;
  float _hotendActual;
  float _hotendTarget;
  float _bedActual;
  float _bedTarget;
  float _progress;
  uint32_t _bytesDone;
  uint32_t _bytesTotal;
  float _posX;
  float _posY;
  float _zHeight;
  float _posE;
  int _feedrate;
  int _flowrate;
  String _printTime;

  // Auto-M108: break through multiple wait_for_user loops
  bool _autoBreakWaits;          // True = auto-send M108 when "paused for user" detected
  uint8_t _autoBreakCount;       // Remaining auto-M108 sends allowed
  uint32_t _lastAutoM108Time;    // Debounce: last time we sent an auto M108
  bool _needAutoM108;            // Flag: set in ISR context, consumed in handle()
  bool _marlinWaitingForUser;    // True when Marlin sends "busy: paused for user"

  // Marlin LCD prompt tracking
  String _marlinPrompt;          // Current prompt text (e.g. "Purge More", "Paused")

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

  void pollTelegramUpdates();
  void processTelegramCommand(String cmd, const String& fromId);
  void sendTelegramReply(const String& text);
};

extern TelegramMonitor telegram_monitor;

#endif
