// ============================================================
// Booth Door Control System - ESP32 Firmware
// Bắt đầu: mở khóa 5s rồi đóng, tạo session
// Kết thúc: mở khóa 5s rồi đóng, kết thúc session
// ============================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

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
#define POLL_INTERVAL_MS   2000
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
unsigned long lastWifiRetry = 0;
unsigned long lastButtonPress = 0;
bool ntpSynced = false;
int wifiFailCount = 0;              // Đếm số lần reconnect thất bại

// ======================== SETUP ==============================
void setup() {
  Serial.begin(115200);
  Serial.println("[boot] starting...");

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);  // Đóng khóa khi khởi động
  Serial.println("[boot] locked");

  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    configTime(7 * 3600, 0, "pool.ntp.org");
    Serial.print("[boot] NTP syncing");
    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo, 1000) && retry < 10) {
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

    // Nếu đây là lệnh kết thúc → end session
    if (endSessionAfterLock) {
      endSessionAfterLock = false;
      pauseSessionAfterLock = false;
      resumeSessionAfterLock = false;
      int durationSec = ((now - sessionStartMillis) - totalPausedMs) / 1000;
      Serial.printf("[session] ending (duration: %ds, paused: %ds)\n", durationSec, (int)(totalPausedMs / 1000));
      endSession(durationSec);
      sessionActive = false;
      sessionPaused = false;
    }
    // Nếu đây là lệnh pause → pause session
    else if (pauseSessionAfterLock) {
      pauseSessionAfterLock = false;
      sessionPaused = true;
      pauseStartMillis = now;
      pauseCount++;
      Serial.printf("[session] paused (count: %d)\n", pauseCount);
      pauseSession();
    }
    // Nếu đây là lệnh resume → resume session
    else if (resumeSessionAfterLock) {
      resumeSessionAfterLock = false;
      unsigned long thisPauseMs = now - pauseStartMillis;
      totalPausedMs += thisPauseMs;
      sessionPaused = false;
      Serial.printf("[session] resumed (this pause: %ds, total paused: %ds)\n",
        (int)(thisPauseMs / 1000), (int)(totalPausedMs / 1000));
      resumeSession();
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

  // --- NTP retry nếu chưa sync ---
  if (!ntpSynced && WiFi.status() == WL_CONNECTED) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
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

  // --- Poll Supabase ---
  if (WiFi.status() == WL_CONNECTED && now - lastPoll >= POLL_INTERVAL_MS) {
    lastPoll = now;
    pollCommands();
  }
}

// ======================== WIFI ===============================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);    // ESP32 tự reconnect ở tầng driver
  WiFi.persistent(true);          // Lưu credentials vào flash

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
    if (wifiFailCount > 0) {
      // Vừa reconnect thành công
      Serial.println("[wifi] reconnected " + WiFi.localIP().toString());
      Serial.printf("[wifi] RSSI: %d dBm (after %d retries)\n", WiFi.RSSI(), wifiFailCount);
      wifiFailCount = 0;

      // Re-sync NTP sau khi reconnect
      configTime(7 * 3600, 0, "pool.ntp.org");
    }
    return;
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
void sendHeartbeat() {
  if (!ntpSynced) return;  // Chờ NTP sync xong mới gửi heartbeat

  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/booths?booth_id=eq." + BOOTH_ID);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.setTimeout(3000);

  String isoTime = getISOTime();
  if (isoTime.length() == 0) return;
  char payload[128];
  snprintf(payload, sizeof(payload), "{\"last_seen\":\"%s\"}", isoTime.c_str());
  http.PATCH(payload);
  http.end();
}

// ======================== POLL COMMANDS ======================
void pollCommands() {
  sendHeartbeat();

  HTTPClient http;
  String url = String(SUPABASE_URL)
    + "/rest/v1/commands?booth_id=eq." + BOOTH_ID
    + "&status=eq.pending&order=created_at.asc&limit=1";

  http.begin(url);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.setTimeout(5000);

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

  markExecuted(id);
  Serial.printf("[cmd] %s (id: %s)\n", action, id);

  if (strcmp(action, "open") == 0 && !sessionActive && !sessionPaused && !unlocking) {
    // Bắt đầu phiên mới
    unlockForStart();
  } else if (strcmp(action, "close") == 0 && (sessionActive || sessionPaused) && !unlocking) {
    // Kết thúc phiên (từ active hoặc paused)
    if (sessionPaused) {
      totalPausedMs += (millis() - pauseStartMillis);
      sessionPaused = false;
    }
    unlockForEnd();
  } else if (strcmp(action, "pause") == 0 && sessionActive && !sessionPaused && !unlocking) {
    // Tạm dừng phiên
    unlockForPause();
  } else if (strcmp(action, "resume") == 0 && sessionPaused && !unlocking) {
    // Tiếp tục phiên
    unlockForResume();
  }
}

// ======================== UNLOCK LOGIC =======================
void unlockForStart() {
  // Mở khóa 5s, tạo session
  digitalWrite(PIN_RELAY, HIGH);
  unlocking = true;
  unlockStartAt = millis();
  endSessionAfterLock = false;
  pauseSessionAfterLock = false;
  resumeSessionAfterLock = false;
  Serial.println("[lock] unlocked for START");

  // Tạo session, reset pause state
  sessionActive = true;
  sessionPaused = false;
  sessionStartMillis = millis();
  totalPausedMs = 0;
  pauseCount = 0;
  createSession();
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
  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/commands?id=eq." + id);
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.setTimeout(5000);

  int code = http.PATCH("{\"status\":\"executed\"}");
  if (code < 200 || code >= 300) {
    Serial.printf("[error] markExecuted PATCH %d\n", code);
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
