// ============================================================
// Booth Door Control System - ESP32 Firmware
// Bắt đầu: mở khóa 5s rồi đóng, tạo session
// Kết thúc: mở khóa 5s rồi đóng, kết thúc session
// ============================================================

#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>  // Cần cài: Library Manager → "WebSockets" by Markus Sattler
#include <HTTPUpdate.h>        // OTA firmware update qua HTTP
#include <time.h>

#define FIRMWARE_VERSION "1.0.1"

// ======================== CREDENTIALS ========================
#define WIFI_SSID       "Epione VP 2.4G"
#define WIFI_PASS       "vpepione2025"
#define SUPABASE_URL    "https://iwczjfomyybszwdlzwjs.supabase.co"
#define SUPABASE_KEY    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Iml3Y3pqZm9teXlic3p3ZGx6d2pzIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzMxNDIzODgsImV4cCI6MjA4ODcxODM4OH0.dvGSX8N5_tHpGqWL5rCreCdGpASxOK2nwwcbCpILew4"
#define BOOTH_ID        "BOOTH_001"

// ======================== PINS ===============================
#define PIN_RELAY   26   // HIGH = mở khóa, LOW = đóng khóa
#define PIN_PIR     27   // Chưa dùng trong MVP
#define PIN_BUTTON  14   // INPUT_PULLUP, nhấn = kết thúc phiên

// ======================== TIMING =============================
#define POLL_INTERVAL_MS   500    // Poll nhanh khi WS mất
#define POLL_FALLBACK_MS   5000   // Poll chậm khi WS hoạt động (chỉ backup)
#define HEARTBEAT_INTERVAL_MS 5000 // Heartbeat HTTP mỗi 5s
#define OTA_CHECK_INTERVAL_MS 3600000UL // Check OTA mỗi 1 giờ - ưu tiên ổn định đóng mở
#define WS_PHOENIX_HB_MS   25000  // Phoenix heartbeat mỗi 25s (< 30s timeout)
#define WIFI_RETRY_MS      5000    // Retry WiFi mỗi 5 giây (giảm từ 30s)
#define WIFI_FULL_RESET_AFTER 3    // Sau 3 lần reconnect fail → full reset WiFi
#define DEBOUNCE_MS        300
#define UNLOCK_DURATION_MS 5000   // Mở khóa 5 giây
#define SESSION_TIMEOUT_MS 3600000UL  // Auto-end session sau 60 phút
#define PAUSE_TIMEOUT_MS   600000UL  // Auto-end nếu pause quá 10 phút

// ======================== STATE ==============================
bool sessionActive = false;        // Đang trong phiên sử dụng
bool sessionPaused = false;        // Phiên đang tạm dừng
String activeSessionId = "";
unsigned long sessionStartMillis = 0;
unsigned long pauseStartMillis = 0;   // Thời điểm bắt đầu pause hiện tại
unsigned long totalPausedMs = 0;      // Tổng thời gian đã pause (ms)
int pauseCount = 0;                   // Số lần pause

bool unlocking = false;            // Khóa đang mở tạm
unsigned long unlockStartAt = 0;
bool endSessionAfterLock = false;  // Sau khi khóa lại thì kết thúc session
bool pauseSessionAfterLock = false;  // Sau khi khóa lại thì pause session
bool resumeSessionAfterLock = false; // Sau khi khóa lại thì resume session

unsigned long lastPoll = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastOtaCheck = 0;
unsigned long lastWifiRetry = 0;

// WebSocket Realtime
WebSocketsClient ws;
bool wsConnected = false;
bool wsJoined = false;
unsigned long lastWsPhoenixHb = 0;
int wsRef = 0;
String lastExecutedCmdId = "";  // Dedup giữa WS và HTTP poll
unsigned long lastButtonPress = 0;
bool ntpSynced = false;
bool otaInProgress = false;  // Khóa mọi thao tác khi đang OTA
bool pendingCreateSession = false;   // Deferred: tạo session sau khi relay đã mở
bool pendingEndSession = false;      // Deferred: kết thúc session sau khi khóa
int pendingEndDuration = 0;
bool pendingPauseSession = false;    // Deferred: pause session
bool pendingResumeSession = false;   // Deferred: resume session
bool pendingMarkExecuted = false;    // Deferred: ghi DB command status
char pendingCmdId[64] = "";
char pendingExecutedTime[32] = "";
char pendingRelayTime[32] = "";
int wifiFailCount = 0;              // Đếm số lần reconnect thất bại
bool wifiWasConnected = true;       // Theo dõi trạng thái trước đó để log disconnect
unsigned long wifiDisconnectAt = 0; // Thời điểm mất WiFi

// ======================== SETUP ==============================
void setup() {
  Serial.begin(115200);
  Serial.printf("[boot] starting... FW v%s\n", FIRMWARE_VERSION);

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);  // Đóng khóa khi khởi động
  Serial.println("[boot] locked");

  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    delay(1000);  // Đợi router ổn định DNS sau khi WiFi connect
    configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    Serial.print("[boot] NTP syncing");
    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo, 2000) && retry < 10) {
      Serial.print(".");
      retry++;
    }
    Serial.println();
    if (retry < 10) {
      ntpSynced = true;
      char buf[30];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
      Serial.printf("[boot] NTP OK: %s\n", buf);
    } else {
      Serial.println("[boot] NTP failed, will retry in loop");
    }

    markAllPendingExecuted();
    closeStaleSession();
    connectRealtime();
  }

  Serial.println("[boot] ready");
}

// ======================== LOOP ===============================
void loop() {
  unsigned long now = millis();

  // --- Tự đóng khóa sau 5s ---
  if (unlocking && now - unlockStartAt >= UNLOCK_DURATION_MS) {
    digitalWrite(PIN_RELAY, LOW);
    unlocking = false;
    Serial.println("[lock] locked back");

    // Nếu đây là lệnh kết thúc → end session (deferred HTTP)
    if (endSessionAfterLock) {
      endSessionAfterLock = false;
      pauseSessionAfterLock = false;
      resumeSessionAfterLock = false;
      pendingEndDuration = ((now - sessionStartMillis) - totalPausedMs) / 1000;
      Serial.printf("[session] ending (duration: %ds, paused: %ds)\n", pendingEndDuration, (int)(totalPausedMs / 1000));
      pendingEndSession = true;  // Deferred: gọi endSession() ở loop tiếp theo
      sessionActive = false;
      sessionPaused = false;
    }
    // Nếu đây là lệnh pause → pause session (deferred HTTP)
    else if (pauseSessionAfterLock) {
      pauseSessionAfterLock = false;
      sessionPaused = true;
      pauseStartMillis = now;
      pauseCount++;
      Serial.printf("[session] paused (count: %d)\n", pauseCount);
      pendingPauseSession = true;  // Deferred
    }
    // Nếu đây là lệnh resume → resume session (deferred HTTP)
    else if (resumeSessionAfterLock) {
      resumeSessionAfterLock = false;
      unsigned long thisPauseMs = now - pauseStartMillis;
      totalPausedMs += thisPauseMs;
      sessionPaused = false;
      Serial.printf("[session] resumed (this pause: %ds, total paused: %ds)\n",
        (int)(thisPauseMs / 1000), (int)(totalPausedMs / 1000));
      pendingResumeSession = true;  // Deferred
    }
  }

  // --- Nút vật lý = luôn mở khóa 5s ---
  // Đọc 3 lần cách 5ms để lọc nhiễu floating
  if (!unlocking && now - lastButtonPress > DEBOUNCE_MS) {
    if (digitalRead(PIN_BUTTON) == LOW) {
      delay(5);
      if (digitalRead(PIN_BUTTON) == LOW) {
        delay(5);
        if (digitalRead(PIN_BUTTON) == LOW) {
          lastButtonPress = now;
          if (sessionActive && !sessionPaused) {
            Serial.println("[button] pressed -> ending session");
            unlockForEnd();
          } else if (sessionPaused) {
            Serial.println("[button] pressed -> ending paused session");
            unlockForEnd();
            logEvent("button_end_paused", "ended session while paused");
          } else {
            Serial.println("[button] pressed -> emergency unlock (no session)");
            unlockEmergency();
          }
        }
      }
    }
  }

  // --- WiFi management ---
  handleWiFi(now);

  // --- NTP retry nếu chưa sync (50ms timeout để không block loop) ---
  if (!ntpSynced && WiFi.status() == WL_CONNECTED) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 50)) {
      ntpSynced = true;
      char buf[30];
      strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
      Serial.printf("[ntp] synced: %s\n", buf);
    }
  }

  // --- Auto-end session sau 60 phút (chỉ tính thời gian active, không tính pause) ---
  if (sessionActive && !sessionPaused && !unlocking) {
    unsigned long activeMs = (now - sessionStartMillis) - totalPausedMs;
    if (activeMs >= SESSION_TIMEOUT_MS) {
      Serial.println("[session] timeout 60min -> auto ending");
      unlockForEnd();
      logEvent("session_timeout", "auto-end after 60 minutes active time");
    }
  }

  // --- Auto-end nếu pause quá 10 phút ---
  if (sessionPaused && !unlocking && now - pauseStartMillis >= PAUSE_TIMEOUT_MS) {
    Serial.println("[session] pause timeout 10min -> auto ending");
    // Cộng thời gian pause cuối vào tổng
    totalPausedMs += (now - pauseStartMillis);
    sessionPaused = false;
    unlockForEnd();
    logEvent("pause_timeout", "auto-end after 10 minutes paused");
  }

  // --- WebSocket loop (non-blocking) ---
  ws.loop();

  // --- Phoenix heartbeat mỗi 25s ---
  if (wsConnected && now - lastWsPhoenixHb >= WS_PHOENIX_HB_MS) {
    lastWsPhoenixHb = now;
    wsPhoenixHeartbeat();
  }

  // --- Nếu đang OTA thì chỉ chạy ws.loop() ---
  if (otaInProgress) return;

  // --- Deferred DB tasks: chỉ chạy 1 task/loop để không block ---
  if (WiFi.status() == WL_CONNECTED && !unlocking) {
    if (pendingMarkExecuted) {
      pendingMarkExecuted = false;
      markExecutedWithTimestamp(pendingCmdId, String(pendingExecutedTime), String(pendingRelayTime));
    } else if (pendingCreateSession) {
      pendingCreateSession = false;
      createSession();
    } else if (pendingEndSession) {
      pendingEndSession = false;
      endSession(pendingEndDuration);
    } else if (pendingPauseSession) {
      pendingPauseSession = false;
      pauseSession();
    } else if (pendingResumeSession) {
      pendingResumeSession = false;
      resumeSession();
    }
  }

  // --- HTTP heartbeat mỗi 5s (chỉ khi không có deferred task) ---
  if (WiFi.status() == WL_CONNECTED && now - lastHeartbeat >= HEARTBEAT_INTERVAL_MS
      && !pendingMarkExecuted && !pendingCreateSession && !pendingEndSession) {
    lastHeartbeat = now;
    sendHeartbeat();
  }

  // --- Check OTA mỗi 1 giờ (tách riêng) ---
  if (WiFi.status() == WL_CONNECTED && now - lastOtaCheck >= OTA_CHECK_INTERVAL_MS) {
    lastOtaCheck = now;
    checkOtaUpdates();
  }

  // --- Poll commands: 500ms nếu WS mất, 5s nếu WS OK (backup) ---
  unsigned long pollInterval = wsConnected ? POLL_FALLBACK_MS : POLL_INTERVAL_MS;
  if (WiFi.status() == WL_CONNECTED && now - lastPoll >= pollInterval) {
    lastPoll = now;
    pollCommands();
  }
}

// ======================== WIFI ===============================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);    // ESP32 tự reconnect ở tầng driver
  WiFi.persistent(true);          // Lưu credentials vào flash
  esp_wifi_set_ps(WIFI_PS_NONE);  // Tắt power saving - booth cắm điện 24/7

  Serial.print("[wifi] connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500);  // Dùng delay() để WiFi stack xử lý event
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiFailCount = 0;
    Serial.println("[wifi] connected " + WiFi.localIP().toString());
    Serial.printf("[wifi] RSSI: %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println("[wifi] failed");
  }
}

void handleWiFi(unsigned long now) {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected) {
      // Vừa reconnect thành công - log lên Supabase
      unsigned long downtime = (now - wifiDisconnectAt) / 1000;
      int rssi = WiFi.RSSI();
      Serial.println("[wifi] reconnected " + WiFi.localIP().toString());
      Serial.printf("[wifi] RSSI: %d dBm (after %d retries, down %lus)\n", rssi, wifiFailCount, downtime);

      // Re-sync NTP sau khi reconnect
      configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");

      // Log reconnect event lên Supabase
      char note[128];
      snprintf(note, sizeof(note), "reconnected after %lus, %d retries, RSSI: %d dBm", downtime, wifiFailCount, rssi);
      logEvent("wifi_reconnect", note);

      // Reconnect WebSocket Realtime
      connectRealtime();

      wifiFailCount = 0;
      wifiWasConnected = true;
    }
    return;
  }

  // Phát hiện mất WiFi lần đầu
  if (wifiWasConnected) {
    wifiWasConnected = false;
    wifiDisconnectAt = now;
    Serial.println("[wifi] disconnected!");
  }

  // WiFi đang mất - retry mỗi 5 giây
  if (now - lastWifiRetry < WIFI_RETRY_MS) return;
  lastWifiRetry = now;
  wifiFailCount++;

  if (wifiFailCount <= WIFI_FULL_RESET_AFTER) {
    // Giai đoạn 1: disconnect sạch rồi reconnect
    Serial.printf("[wifi] reconnecting... (attempt %d)\n", wifiFailCount);
    WiFi.disconnect();
    delay(100);
    WiFi.reconnect();
  } else {
    // Giai đoạn 2: full reset WiFi stack - begin() lại từ đầu
    Serial.printf("[wifi] full reset WiFi (attempt %d)\n", wifiFailCount);
    WiFi.disconnect(true);  // true = xóa credentials khỏi RAM
    delay(500);
    WiFi.mode(WIFI_OFF);
    delay(500);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

// ======================== HEARTBEAT ==========================
unsigned long lastRssiWarnAt = 0;
#define RSSI_WARN_THRESHOLD -75     // dBm - dưới mức này là tín hiệu yếu
#define RSSI_WARN_INTERVAL  300000  // Chỉ log cảnh báo RSSI mỗi 5 phút

void sendHeartbeat() {
  if (!ntpSynced) return;  // Chờ NTP sync xong mới gửi heartbeat

  // Cảnh báo RSSI yếu (mỗi 5 phút)
  int rssi = WiFi.RSSI();
  unsigned long now = millis();
  if (rssi < RSSI_WARN_THRESHOLD && now - lastRssiWarnAt >= RSSI_WARN_INTERVAL) {
    lastRssiWarnAt = now;
    Serial.printf("[wifi] weak signal: %d dBm\n", rssi);
    char note[64];
    snprintf(note, sizeof(note), "RSSI: %d dBm (weak signal)", rssi);
    logEvent("wifi_weak_signal", note);
  }

  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/booths?booth_id=eq." + BOOTH_ID);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.setTimeout(3000);

  String isoTime = getISOTime();
  if (isoTime.length() == 0) return;
  char payload[192];
  snprintf(payload, sizeof(payload),
    "{\"last_seen\":\"%s\",\"firmware_version\":\"%s\"}",
    isoTime.c_str(), FIRMWARE_VERSION);
  http.PATCH(payload);
  http.end();
}

// ======================== SUPABASE REALTIME (WebSocket) ======
void connectRealtime() {
  String host = String(SUPABASE_URL);
  host.replace("https://", "");  // chỉ lấy hostname

  String path = "/realtime/v1/websocket?apikey=" + String(SUPABASE_KEY) + "&vsn=1.0.0";

  ws.beginSSL(host.c_str(), 443, path.c_str());
  ws.onEvent(wsEvent);
  ws.setReconnectInterval(3000);
  Serial.println("[ws] connecting...");
}

void wsEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsConnected = true;
      wsJoined = false;
      Serial.println("[ws] connected");
      wsJoinChannel();
      break;

    case WStype_DISCONNECTED:
      wsConnected = false;
      wsJoined = false;
      Serial.println("[ws] disconnected");
      break;

    case WStype_TEXT:
      handleRealtimeMessage((char*)payload);
      break;

    case WStype_PING:
    case WStype_PONG:
      break;
  }
}

void wsJoinChannel() {
  wsRef++;
  char msg[512];
  snprintf(msg, sizeof(msg),
    "{\"topic\":\"realtime:booth-%s\","
    "\"event\":\"phx_join\","
    "\"payload\":{\"config\":{\"postgres_changes\":[{"
      "\"event\":\"INSERT\","
      "\"schema\":\"public\","
      "\"table\":\"commands\","
      "\"filter\":\"booth_id=eq.%s\""
    "}]}},"
    "\"ref\":\"%d\"}",
    BOOTH_ID, BOOTH_ID, wsRef);

  ws.sendTXT(msg);
  Serial.printf("[ws] join channel: booth-%s\n", BOOTH_ID);
}

void wsPhoenixHeartbeat() {
  wsRef++;
  char msg[96];
  snprintf(msg, sizeof(msg),
    "{\"topic\":\"phoenix\",\"event\":\"heartbeat\",\"payload\":{},\"ref\":\"%d\"}", wsRef);
  ws.sendTXT(msg);
}

void handleRealtimeMessage(const char* raw) {
  JsonDocument doc;
  if (deserializeJson(doc, raw)) return;

  const char* event = doc["event"];
  if (!event) return;

  // Phoenix join reply
  if (strcmp(event, "phx_reply") == 0) {
    const char* status = doc["payload"]["status"];
    if (status && strcmp(status, "ok") == 0) {
      wsJoined = true;
      Serial.println("[ws] joined OK - realtime active!");
    } else {
      Serial.printf("[ws] join reply: %s\n", status ? status : "error");
    }
    return;
  }

  // Postgres INSERT event
  if (strcmp(event, "postgres_changes") == 0) {
    JsonObject data = doc["payload"]["data"];
    const char* type = data["type"];

    if (type && strcmp(type, "INSERT") == 0) {
      JsonObject record = data["record"];
      const char* id = record["id"];
      const char* action = record["action"];
      const char* cmdStatus = record["status"];
      const char* boothId = record["booth_id"];

      if (id && action && cmdStatus
          && strcmp(cmdStatus, "pending") == 0
          && strcmp(boothId, BOOTH_ID) == 0) {
        Serial.printf("[ws] REALTIME cmd: %s (id: %s)\n", action, id);
        executeCommand(id, action);
      }
    }
    return;
  }
}

// Shared command execution (dùng bởi cả WS và HTTP poll)
void executeCommand(const char* id, const char* action) {
  // Dedup: skip nếu đã xử lý command này
  if (lastExecutedCmdId == String(id)) {
    Serial.printf("[cmd] skip duplicate: %s\n", id);
    return;
  }
  lastExecutedCmdId = String(id);

  String executedTime = getISOTime();
  Serial.printf("[cmd] execute: %s (id: %s)\n", action, id);

  // MỞ RELAY TRƯỚC - ưu tiên tốc độ, không block HTTP
  bool acted = false;
  if (strcmp(action, "open") == 0 && !sessionActive && !sessionPaused && !unlocking) {
    unlockForStart();
    acted = true;
  } else if (strcmp(action, "close") == 0 && (sessionActive || sessionPaused) && !unlocking) {
    if (sessionPaused) {
      totalPausedMs += (millis() - pauseStartMillis);
      sessionPaused = false;
    }
    unlockForEnd();
    acted = true;
  } else if (strcmp(action, "pause") == 0 && sessionActive && !sessionPaused && !unlocking) {
    unlockForPause();
    acted = true;
  } else if (strcmp(action, "resume") == 0 && sessionPaused && !unlocking) {
    unlockForResume();
    acted = true;
  }

  // GHI DB SAU (deferred - không block relay)
  String relayTime = acted ? getISOTime() : "";
  strncpy(pendingCmdId, id, sizeof(pendingCmdId) - 1);
  strncpy(pendingExecutedTime, executedTime.c_str(), sizeof(pendingExecutedTime) - 1);
  strncpy(pendingRelayTime, relayTime.c_str(), sizeof(pendingRelayTime) - 1);
  pendingMarkExecuted = true;  // Sẽ gọi markExecuted ở loop tiếp theo
}

// ======================== POLL COMMANDS (fallback) ============
void pollCommands() {
  HTTPClient http;
  String url = String(SUPABASE_URL)
    + "/rest/v1/commands?booth_id=eq." + BOOTH_ID
    + "&status=eq.pending&order=created_at.asc&limit=1";

  http.begin(url);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.setTimeout(3000);

  int code = http.GET();

  if (code != 200) {
    if (code > 0) Serial.printf("[error] poll GET %d\n", code);
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() == 0) return;

  const char* id = arr[0]["id"];
  const char* action = arr[0]["action"];

  Serial.printf("[poll] found cmd: %s (id: %s)\n", action, id);
  executeCommand(id, action);
}

// ======================== UNLOCK LOGIC =======================
void unlockForStart() {
  // Mở khóa 5s, tạo session (deferred - không block relay)
  digitalWrite(PIN_RELAY, HIGH);
  unlocking = true;
  unlockStartAt = millis();
  endSessionAfterLock = false;
  pauseSessionAfterLock = false;
  resumeSessionAfterLock = false;
  Serial.println("[lock] unlocked for START");

  // Set state ngay, nhưng HTTP call sẽ chạy deferred
  sessionActive = true;
  sessionPaused = false;
  sessionStartMillis = millis();
  totalPausedMs = 0;
  pauseCount = 0;
  pendingCreateSession = true;  // Sẽ gọi createSession() ở loop tiếp theo
}

void unlockForEnd() {
  // Mở khóa 5s, sau đó kết thúc session
  digitalWrite(PIN_RELAY, HIGH);
  unlocking = true;
  unlockStartAt = millis();
  endSessionAfterLock = true;
  pauseSessionAfterLock = false;
  resumeSessionAfterLock = false;
  Serial.println("[lock] unlocked for END");
}

void unlockForPause() {
  // Mở khóa 5s, sau đó pause session
  digitalWrite(PIN_RELAY, HIGH);
  unlocking = true;
  unlockStartAt = millis();
  endSessionAfterLock = false;
  pauseSessionAfterLock = true;
  resumeSessionAfterLock = false;
  Serial.println("[lock] unlocked for PAUSE");
}

void unlockForResume() {
  // Mở khóa 5s, sau đó resume session
  digitalWrite(PIN_RELAY, HIGH);
  unlocking = true;
  unlockStartAt = millis();
  endSessionAfterLock = false;
  pauseSessionAfterLock = false;
  resumeSessionAfterLock = true;
  Serial.println("[lock] unlocked for RESUME");
}

void unlockEmergency() {
  // Mở khóa 5s nhưng không tạo/kết thúc session
  digitalWrite(PIN_RELAY, HIGH);
  unlocking = true;
  unlockStartAt = millis();
  endSessionAfterLock = false;
  pauseSessionAfterLock = false;
  resumeSessionAfterLock = false;
  Serial.println("[lock] unlocked EMERGENCY (no session)");

  // Ghi log lên Supabase để biết có lần mở khẩn cấp ngoài phiên
  logEvent("emergency_button", "unlock outside session");
}

void logEvent(const char* event, const char* note) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/booth_logs");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.setTimeout(5000);

  JsonDocument doc;
  doc["booth_id"] = BOOTH_ID;
  doc["event"] = event;
  doc["note"] = note;
  String body;
  serializeJson(doc, body);

  http.POST(body);
  http.end();
}

// ======================== SUPABASE HELPERS ====================
void markExecuted(const char* id) {
  markExecutedWithTimestamp(id, "", "");
}

void markExecutedWithTimestamp(const char* id, String executedAt, String relayAt) {
  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/commands?id=eq." + id);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.setTimeout(3000);

  char payload[256];
  if (executedAt.length() > 0 && relayAt.length() > 0) {
    snprintf(payload, sizeof(payload),
      "{\"status\":\"executed\",\"executed_at\":\"%s\",\"relay_at\":\"%s\"}",
      executedAt.c_str(), relayAt.c_str());
  } else if (executedAt.length() > 0) {
    snprintf(payload, sizeof(payload),
      "{\"status\":\"executed\",\"executed_at\":\"%s\"}",
      executedAt.c_str());
  } else {
    snprintf(payload, sizeof(payload), "{\"status\":\"executed\"}");
  }

  int code = http.PATCH(payload);
  if (code < 200 || code >= 300) {
    Serial.printf("[error] markExecuted PATCH %d\n", code);
  } else {
    // Log latency nếu có timestamps
    if (relayAt.length() > 0) {
      Serial.printf("[latency] executed_at=%s relay_at=%s\n", executedAt.c_str(), relayAt.c_str());
    }
  }
  http.end();
}

void markAllPendingExecuted() {
  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/commands?booth_id=eq." + BOOTH_ID + "&status=eq.pending");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.setTimeout(5000);
  http.PATCH("{\"status\":\"executed\"}");
  http.end();
  Serial.println("[boot] cleared pending commands");
}

void createSession() {
  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/sessions");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=representation");
  http.setTimeout(5000);

  JsonDocument doc;
  doc["booth_id"] = BOOTH_ID;
  doc["status"] = "active";
  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  if (code >= 200 && code < 300) {
    String resp = http.getString();
    JsonDocument respDoc;
    if (!deserializeJson(respDoc, resp)) {
      if (respDoc.is<JsonArray>() && respDoc.as<JsonArray>().size() > 0) {
        activeSessionId = String((const char*)respDoc[0]["id"]);
      } else if (respDoc.containsKey("id")) {
        activeSessionId = String((const char*)respDoc["id"]);
      }
    }
    Serial.println("[session] created: " + activeSessionId);
  } else {
    Serial.printf("[error] createSession POST %d\n", code);
  }
  http.end();
}

void pauseSession() {
  if (activeSessionId.length() == 0) return;

  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/sessions?id=eq." + activeSessionId);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.setTimeout(5000);

  String isoTime = getISOTime();
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"status\":\"paused\",\"paused_at\":\"%s\",\"pause_count\":%d}",
    isoTime.c_str(), pauseCount);

  int code = http.PATCH(payload);
  if (code < 200 || code >= 300) {
    Serial.printf("[error] pauseSession PATCH %d\n", code);
  } else {
    Serial.println("[session] paused in DB: " + activeSessionId);
  }
  http.end();
}

void resumeSession() {
  if (activeSessionId.length() == 0) return;

  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/sessions?id=eq." + activeSessionId);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.setTimeout(5000);

  int totalPausedSec = totalPausedMs / 1000;
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"status\":\"active\",\"paused_at\":null,\"total_paused_seconds\":%d}",
    totalPausedSec);

  int code = http.PATCH(payload);
  if (code < 200 || code >= 300) {
    Serial.printf("[error] resumeSession PATCH %d\n", code);
  } else {
    Serial.println("[session] resumed in DB: " + activeSessionId);
  }
  http.end();
}

void endSession(int durationSec) {
  if (activeSessionId.length() == 0) {
    Serial.println("[session] no active session to end");
    return;
  }

  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/sessions?id=eq." + activeSessionId);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.setTimeout(5000);

  String isoTime = getISOTime();
  int totalPausedSec = totalPausedMs / 1000;
  char payload[512];
  if (isoTime.length() > 0) {
    snprintf(payload, sizeof(payload),
      "{\"ended_at\":\"%s\",\"duration_seconds\":%d,\"status\":\"ended\",\"paused_at\":null,\"total_paused_seconds\":%d,\"pause_count\":%d}",
      isoTime.c_str(), durationSec, totalPausedSec, pauseCount);
  } else {
    snprintf(payload, sizeof(payload),
      "{\"duration_seconds\":%d,\"status\":\"ended\",\"paused_at\":null,\"total_paused_seconds\":%d,\"pause_count\":%d}",
      durationSec, totalPausedSec, pauseCount);
  }

  int code = http.PATCH(payload);
  if (code < 200 || code >= 300) {
    Serial.printf("[error] endSession PATCH %d\n", code);
  } else {
    Serial.println("[session] ended: " + activeSessionId);
  }
  http.end();
  activeSessionId = "";
  totalPausedMs = 0;
  pauseCount = 0;
}

void closeStaleSession() {
  // Đóng cả session active và paused khi boot
  String isoTime = getISOTime();
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"ended_at\":\"%s\",\"status\":\"ended\",\"paused_at\":null}", isoTime.c_str());

  // Close active sessions
  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/sessions?booth_id=eq." + BOOTH_ID + "&status=eq.active");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.setTimeout(5000);
  http.PATCH(payload);
  http.end();

  // Close paused sessions
  HTTPClient http2;
  http2.begin(String(SUPABASE_URL) + "/rest/v1/sessions?booth_id=eq." + BOOTH_ID + "&status=eq.paused");
  http2.addHeader("apikey", SUPABASE_KEY);
  http2.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http2.addHeader("Content-Type", "application/json");
  http2.addHeader("Prefer", "return=minimal");
  http2.setTimeout(5000);
  http2.PATCH(payload);
  http2.end();

  Serial.println("[boot] closed stale sessions (active + paused)");
}

// ======================== OTA FIRMWARE UPDATE ================
void performOTA(const char* firmwareUrl, const char* otaUpdateId) {
  if (otaInProgress) return;
  if (sessionActive || unlocking) {
    Serial.println("[ota] skipped - session active or door unlocking");
    updateOtaStatus(otaUpdateId, "failed", "session active");
    return;
  }

  otaInProgress = true;
  Serial.printf("[ota] starting download: %s\n", firmwareUrl);
  updateOtaStatus(otaUpdateId, "downloading", "");

  // Update ota_status trên booths table
  updateBoothOtaStatus("downloading", 0);

  WiFiClient client;
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false);  // Không reboot tự động - ta muốn log trước

  t_httpUpdate_return ret = httpUpdate.update(client, String(firmwareUrl));

  switch (ret) {
    case HTTP_UPDATE_OK:
      Serial.println("[ota] success! rebooting...");
      updateOtaStatus(otaUpdateId, "success", "");
      updateBoothOtaStatus("success", 100);
      logEvent("ota_success", firmwareUrl);
      delay(500);
      ESP.restart();
      break;

    case HTTP_UPDATE_FAILED:
      Serial.printf("[ota] failed: %s\n", httpUpdate.getLastErrorString().c_str());
      {
        char err[128];
        snprintf(err, sizeof(err), "%s", httpUpdate.getLastErrorString().c_str());
        updateOtaStatus(otaUpdateId, "failed", err);
        updateBoothOtaStatus("failed", 0);
        logEvent("ota_failed", err);
      }
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("[ota] no update available");
      updateOtaStatus(otaUpdateId, "failed", "no update available");
      updateBoothOtaStatus("failed", 0);
      break;
  }

  otaInProgress = false;
}

void updateOtaStatus(const char* otaUpdateId, const char* status, const char* errorMsg) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/ota_updates?id=eq." + otaUpdateId);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.setTimeout(5000);

  char payload[512];
  if (strcmp(status, "success") == 0 || strcmp(status, "failed") == 0) {
    String isoTime = getISOTime();
    snprintf(payload, sizeof(payload),
      "{\"status\":\"%s\",\"error_message\":\"%s\",\"completed_at\":\"%s\"}",
      status, errorMsg, isoTime.c_str());
  } else {
    snprintf(payload, sizeof(payload),
      "{\"status\":\"%s\",\"error_message\":\"%s\"}",
      status, errorMsg);
  }

  http.PATCH(payload);
  http.end();
}

void updateBoothOtaStatus(const char* status, int progress) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/booths?booth_id=eq." + BOOTH_ID);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.setTimeout(3000);

  char payload[128];
  snprintf(payload, sizeof(payload),
    "{\"ota_status\":\"%s\",\"ota_progress\":%d}", status, progress);

  http.PATCH(payload);
  http.end();
}

// Check pending OTA trên server (poll mỗi heartbeat cycle)
void checkOtaUpdates() {
  if (otaInProgress || sessionActive || unlocking) return;

  HTTPClient http;
  String url = String(SUPABASE_URL)
    + "/rest/v1/ota_updates?booth_id=eq." + BOOTH_ID
    + "&status=eq.pending&order=started_at.desc&limit=1"
    + "&select=id,firmware_id,firmwares(file_url,version)";

  http.begin(url);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.setTimeout(3000);

  int code = http.GET();
  if (code != 200) {
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) return;
  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() == 0) return;

  const char* otaId = arr[0]["id"];
  JsonObject fw = arr[0]["firmwares"];
  const char* fileUrl = fw["file_url"];
  const char* version = fw["version"];

  if (!otaId || !fileUrl) return;

  // Skip nếu version giống hiện tại
  if (version && strcmp(version, FIRMWARE_VERSION) == 0) {
    Serial.printf("[ota] already on version %s, skipping\n", version);
    updateOtaStatus(otaId, "success", "already up to date");
    return;
  }

  Serial.printf("[ota] new firmware available: %s -> %s\n", FIRMWARE_VERSION, version);
  performOTA(fileUrl, otaId);
}

// ISO 8601 timestamp từ NTP
String getISOTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 500)) {
    return "";  // Trả rỗng nếu chưa sync
  }
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S+07:00", &timeinfo);
  return String(buf);
}
