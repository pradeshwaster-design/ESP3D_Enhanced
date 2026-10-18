/*
  telegram_monitor.h - 2-Way Interactive Telegram Monitor & Remote Control
  
  V6 - Hardware-Verified Reboot & Strict Halt Enforcement:
  - M112 Emergency Stop permanently halts the printer CPU (STM32 cli())
  - Bot NEVER assumes the printer is alive until proven by serial boot message or M105/M115 response
  - Auto-detects physical power cycle / reset via Marlin boot messages (echo:start, SD card ok)
  - Active ping verification via /reboot or /check
  - Blocks all motion, heating, and print commands while halted
  - Single-tap clickable commands: /preheat_pla, /preheat_petg, /home_all, /print_<n>
  - Strict validation for /purge, /continue, /load, /unload
*/

#ifndef _TELEGRAM_MONITOR_H
#define _TELEGRAM_MONITOR_H

#include <Arduino.h>

// ── Configuration ────────────────────────────────────────
#define TELEGRAM_PRINT_INTERVAL  60000   // 1 minute while printing
#define TELEGRAM_IDLE_INTERVAL   300000  // 5 minutes when idle
#define TELEGRAM_POLL_INTERVAL   4000    // Check Telegram commands every 4s
#define MAX_SD_FILES             25
#define AUTO_M108_COOLDOWN       2000    // Min ms between auto-prompt responses
#define AUTO_M108_MAX_COUNT      6       // Max auto-responses per resume/stop

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
  bool _isKilled;               // True when M112 emergency stop / Marlin halt is active
  bool _checkingAlive;          // True when /reboot or /check is waiting for response
  uint32_t _checkAliveTime;     // Timestamp when check was initiated
  bool _notifyReboot;           // Flag: set in ISR when boot signature seen, handled in handle()
  bool _notifyHalt;             // Flag: set in ISR when halt signature seen, handled in handle()

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

  // Auto-break: break through multiple wait_for_user and prompt loops
  bool _autoBreakWaits;          // True = auto-send M876 S1 + M108 when prompt detected
  uint8_t _autoBreakCount;       // Remaining auto-responses allowed
  uint32_t _lastAutoM108Time;    // Debounce timer
  bool _needAutoM108;            // Flag: set in ISR context, consumed in handle()
  bool _marlinWaitingForUser;    // True when Marlin sends "busy: paused for user"

  // Marlin LCD prompt tracking
  String _marlinPrompt;          // Current prompt text (e.g. "Purge More | Continue")

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
