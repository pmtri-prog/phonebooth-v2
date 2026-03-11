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
#define WIFI_SSID       "UYEN TRI"
#define WIFI_PASS       "30101994"
#define SUPABASE_URL    "https://iwczjfomyybszwdlzwjs.supabase.co"
#define SUPABASE_KEY    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Iml3Y3pqZm9teXlic3p3ZGx6d2pzIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzMxNDIzODgsImV4cCI6MjA4ODcxODM4OH0.dvGSX8N5_tHpGqWL5rCreCdGpASxOK2nwwcbCpILew4"
#define BOOTH_ID        "BOOTH_001"

// ======================== PINS ===============================
#define PIN_RELAY   26   // HIGH = mở khóa, LOW = đóng khóa
#define PIN_PIR     27   // Chưa dùng trong MVP
#define PIN_BUTTON  14   // INPUT_PULLUP, nhấn = kết thúc phiên

// ======================== TIMING =============================
#define POLL_INTERVAL_MS   2000
#define WIFI_RETRY_MS      30000
#define DEBOUNCE_MS        300
#define UNLOCK_DURATION_MS 5000   // Mở khóa 5 giây
#define SESSION_TIMEOUT_MS 3600000UL  // Auto-end session sau 60 phút

// ======================== STATE ==============================
bool sessionActive = false;        // Đang trong phiên sử dụng
String activeSessionId = "";
unsigned long sessionStartMillis = 0;

bool unlocking = false;            // Khóa đang mở tạm
unsigned long unlockStartAt = 0;
bool endSessionAfterLock = false;  // Sau khi khóa lại thì kết thúc session

unsigned long lastPoll = 0;
unsigned long lastWifiRetry = 0;
unsigned long lastButtonPress = 0;
bool ntpSynced = false;

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
      int durationSec = (now - sessionStartMillis) / 1000;
      Serial.printf("[session] ending (duration: %ds)\n", durationSec);
      endSession(durationSec);
      sessionActive = false;
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
          if (sessionActive) {
            Serial.println("[button] pressed -> ending session");
            unlockForEnd();
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

  // --- Auto-end session sau 60 phút ---
  if (sessionActive && !unlocking && now - sessionStartMillis >= SESSION_TIMEOUT_MS) {
    Serial.println("[session] timeout 60min -> auto ending");
    unlockForEnd();
    logEvent("session_timeout", "auto-end after 60 minutes");
  }

  // --- Poll Supabase ---
  if (WiFi.status() == WL_CONNECTED && now - lastPoll >= POLL_INTERVAL_MS) {
    lastPoll = now;
    pollCommands();
  }
}

// ======================== WIFI ===============================
void connectWiFi() {
  Serial.print("[wifi] connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    unsigned long w = millis();
    while (millis() - w < 500) {}
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[wifi] connected " + WiFi.localIP().toString());
  } else {
    Serial.println("[wifi] failed");
  }
}

void handleWiFi(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED && now - lastWifiRetry >= WIFI_RETRY_MS) {
    lastWifiRetry = now;
    Serial.println("[wifi] reconnecting...");
    WiFi.reconnect();
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

  if (strcmp(action, "open") == 0 && !sessionActive && !unlocking) {
    // Bắt đầu phiên mới
    unlockForStart();
  } else if (strcmp(action, "close") == 0 && sessionActive && !unlocking) {
    // Kết thúc phiên
    unlockForEnd();
  }
}

// ======================== UNLOCK LOGIC =======================
void unlockForStart() {
  // Mở khóa 5s, tạo session
  digitalWrite(PIN_RELAY, HIGH);
  unlocking = true;
  unlockStartAt = millis();
  endSessionAfterLock = false;  // Không kết thúc session sau khi khóa
  Serial.println("[lock] unlocked for START");

  // Tạo session
  sessionActive = true;
  sessionStartMillis = millis();
  createSession();
}

void unlockForEnd() {
  // Mở khóa 5s, sau đó kết thúc session
  digitalWrite(PIN_RELAY, HIGH);
  unlocking = true;
  unlockStartAt = millis();
  endSessionAfterLock = true;  // Kết thúc session sau khi khóa lại
  Serial.println("[lock] unlocked for END");
}

void unlockEmergency() {
  // Mở khóa 5s nhưng không tạo/kết thúc session
  digitalWrite(PIN_RELAY, HIGH);
  unlocking = true;
  unlockStartAt = millis();
  endSessionAfterLock = false;
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
  char payload[256];
  if (isoTime.length() > 0) {
    snprintf(payload, sizeof(payload),
      "{\"ended_at\":\"%s\",\"duration_seconds\":%d,\"status\":\"ended\"}",
      isoTime.c_str(), durationSec);
  } else {
    snprintf(payload, sizeof(payload),
      "{\"duration_seconds\":%d,\"status\":\"ended\"}", durationSec);
  }

  int code = http.PATCH(payload);
  if (code < 200 || code >= 300) {
    Serial.printf("[error] endSession PATCH %d\n", code);
  } else {
    Serial.println("[session] ended: " + activeSessionId);
  }
  http.end();
  activeSessionId = "";
}

void closeStaleSession() {
  HTTPClient http;
  http.begin(String(SUPABASE_URL) + "/rest/v1/sessions?booth_id=eq." + BOOTH_ID + "&status=eq.active");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  http.setTimeout(5000);

  String isoTime = getISOTime();
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"ended_at\":\"%s\",\"status\":\"ended\"}", isoTime.c_str());
  http.PATCH(payload);
  http.end();
  Serial.println("[boot] closed stale sessions");
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
