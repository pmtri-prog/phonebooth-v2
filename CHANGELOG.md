# Phonebooth MVP - Changelog

## [2026-03-12] Tắt WiFi power saving + nới heartbeat timeout

### Bug fix
- **WiFi power saving gây mất kết nối**: ESP32 mặc định bật modem sleep -> tự tắt WiFi radio -> mất kết nối ngẫu nhiên. Booth cắm điện 24/7 không cần tiết kiệm pin
- **Heartbeat threshold quá chặt (10s)**: Khi mạng hơi chậm, ESP32 heartbeat bị trễ vài giây -> web app báo offline sai

### Thay doi
- `esp_wifi_set_ps(WIFI_PS_NONE)` - tắt WiFi power saving hoàn toàn
- `#include <esp_wifi.h>` - header cho hàm trên
- `HEARTBEAT_TIMEOUT_S`: 10 -> 20 (cả index.html và admin.html)

### File thay doi
- `booth_esp32_3/booth_esp32_3.ino`, `index.html`, `admin.html`

---

## [2026-03-12] WiFi Reconnect cải thiện

### Bug fix
- **WiFi reconnect bị kẹt**: `handleWiFi()` chỉ gọi `WiFi.reconnect()` mà không disconnect trước -> ESP32 bị kẹt ở trạng thái nửa kết nối, không bao giờ reconnect được
- **Busy-wait block WiFi stack**: `connectWiFi()` dùng busy-wait loop (`while (millis() - w < 500) {}`) thay vì `delay()` -> WiFi stack không được xử lý event, gây fail kết nối
- **Retry quá chậm (30s)**: Mất WiFi phải đợi 30 giây mới thử lại -> booth offline quá lâu trên app

### Thay doi
- `WIFI_RETRY_MS`: 30000 -> 5000 (retry mỗi 5s thay vì 30s)
- `connectWiFi()`: Thêm `WiFi.mode(WIFI_STA)`, `WiFi.setAutoReconnect(true)`, `WiFi.persistent(true)`, đổi busy-wait sang `delay(500)`
- `handleWiFi()`: 2 giai đoạn reconnect:
  - Lần 1-3: `WiFi.disconnect()` + `WiFi.reconnect()` (nhẹ)
  - Lần 4+: Full reset WiFi stack (`WIFI_OFF` -> `WIFI_STA` -> `WiFi.begin()`)
- Re-sync NTP tự động sau khi reconnect thành công
- Log RSSI (cường độ tín hiệu) để debug

### File thay doi
- `booth_esp32_3/booth_esp32_3.ino`

---

## [2026-01-08] Location filter + Admin remote control

### Tinh nang
- Thêm filter booth theo location trên admin dashboard
- Admin có thể điều khiển booth từ xa (open/close/pause/resume)

### File thay doi
- `admin.html`, `migration_location.sql`

---

## [2026-01-07] Feedback 3 tiêu chí

### Tinh nang
- Thêm feedback sau khi kết thúc phiên: cách âm, thoải mái, app mượt
- Rating 1-5 sao cho mỗi tiêu chí

### File thay doi
- `index.html`

---

## [2026-01-06] Poll-based state management

### Bug fix
- Revert về poll-based thay vì optimistic UI (gây lệch state giữa app và ESP32)
- Fix state transitions không đồng bộ khi dùng optimistic update

### File thay doi
- `index.html`, `booth_esp32_3/booth_esp32_3.ino`
