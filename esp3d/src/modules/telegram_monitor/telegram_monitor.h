/*
  telegram_monitor.h - 2-Way Interactive Telegram Monitor & Remote Control
  
  V8 - Robust Non-Blocking G-Code SD Uploader & Stop Cooldown Fix:
  - Non-blocking state machine for Telegram G-code upload via Serial M28/M29
  - Full Marlin XOR checksum protocol compliance (N<line> <cmd>*<cs>\n)
  - Slicer comment stripping (reduces transfer time & SD writes by 40%)
  - Guaranteed M29 SD file closure on completion, timeout, or stop/emergency abort
  - Non-blocking slice streaming preserves ESP32 web server responsiveness & feeds TWDT
  - Stop Cooldown Guard (6s) suppressing trailing Marlin auto-reports after /stop
  - Live Tuning: /speed <pct>, /flow <pct>, /fan <pct>, /zoffset <+/-val>
  - Layer progress tracking (;LAYER:<num>, ;LAYER_COUNT:<num>, M117 Layer X/Y)
  - Full system & printer diagnostic command: /info
  - Hardware-verified reboot detection and M112 emergency halt lock
  - Clickable single-tap commands and direct /print_<n> links
*/

#ifndef _TELEGRAM_MONITOR_H
#define _TELEGRAM_MONITOR_H

#include <Arduino.h>
#include <WiFiClientSecure.h>

// ── Configuration ────────────────────────────────────────
#define TELEGRAM_PRINT_INTERVAL  60000   // 1 minute while printing
#define TELEGRAM_IDLE_INTERVAL   300000  // 5 minutes when idle
#define TELEGRAM_POLL_INTERVAL   4000    // Check Telegram commands every 4s
#define MAX_SD_FILES             25
#define AUTO_M108_COOLDOWN       2000    // Min ms between auto-prompt responses
#define AUTO_M108_MAX_COUNT      6       // Max auto-responses per resume/stop

enum UploadState {
  UPLOAD_IDLE = 0,
  UPLOAD_RESOLVE_URL,
  UPLOAD_CONNECT_STREAM,
  UPLOAD_STREAMING
};

class TelegramMonitor {
 public:
  TelegramMonitor();
  void begin();
  void handle();
  void parseSerialData(const uint8_t* data, size_t size);
  bool isUploading() const { return _uploadState != UPLOAD_IDLE; }

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

  // Non-blocking G-code file upload state machine
  UploadState _uploadState;
  WiFiClientSecure _uploadClient;
  String _uploadFileId;
  String _uploadFileName;
  String _uploadFilePath;
  String _uploadSdName;
  uint32_t _uploadFileSize;
  uint32_t _uploadBytesSent;
  uint32_t _uploadLineNum;
  String _uploadLineBuffer;
  uint32_t _uploadLastActivity;
  int _uploadInFlight;
  uint32_t _uploadLastOkTime;

  // Stop cooldown guard (suppresses trailing SD progress reports after /stop)
  uint32_t _stopCooldownUntil;

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
  int _fanPct;
  int _currentLayer;
  int _totalLayers;
  String _printTime;

  // Machine info from M115 & M851
  String _firmwareVersion;
  String _machineType;
  String _zOffset;

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
  void sendPrinterInfoReply();
  void startFileUpload(const String& fileId, const String& fileName, uint32_t fileSize);
  void handleTelegramFileUpload();

  void pollTelegramUpdates();
  void processTelegramCommand(String cmd, const String& fromId);
  void sendTelegramReply(const String& text);
};

extern TelegramMonitor telegram_monitor;

#endif
