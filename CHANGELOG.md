# Phonebooth MVP - Changelog

## [2026-03-14] Rank System & Leaderboard

### Tính năng
- **Hệ thống thăng hạng**: 6 hạng (Tập sự → Bạc → Vàng → Bạch kim → Kim cương → Thách đấu) dựa trên tổng phút deep work
- **Rank card sau phiên**: Hiện +điểm, hạng hiện tại, thanh tiến trình ngay trong completion overlay
- **Bảng xếp hạng (Leaderboard)**: Thay thế trang Thống kê, hiện top 50 deep workers
- **Trang Profile (Tôi)**: Chọn nickname, avatar icon, toggle ẩn/hiện leaderboard
- **18 avatar icons**: Bộ icon dễ thương để user chọn
- **Tab bar mới**: Đổi "Thống kê" → "Xếp hạng" với icon trophy

### Database
- Thêm cột `nickname`, `avatar_icon`, `show_leaderboard` vào users
- Update `login_user()` và `register_user()` trả thêm rank fields
- RPC `get_leaderboard()` cho query leaderboard theo location

### File thay đổi
- `index.html` (Rank system, Leaderboard, Profile page, completion rank card)
- `migration_rank.sql` (NEW - chạy trong Supabase SQL Editor)

---

## [2026-03-14] Demographics Report cho khách hàng quảng cáo

### Tính năng
- **Demographics section** trong admin Reports: phân tích user profile cho brand
- **Donut charts**: Phân bố nghề nghiệp + mục đích sử dụng
- **Conversion bars**: So sánh conversion rate theo từng nhóm demographic
- **Usage patterns**: Phân bố giờ sử dụng (24h) + ngày trong tuần
- **Smart insights**: Tự động highlight nhóm user chính + conversion cao nhất
- **Filter theo địa điểm**: Lọc demographics theo location/booth/brand
- **Export CSV**: Xuất dữ liệu demographics cho từng user
- **Summary cards**: Unique users, profile rate, sessions, impressions

### File thay đổi
- `admin.html` (Demographics section: HTML + CSS + JS)

---

## [2026-03-14] Booth Reservation (Giữ chỗ 10 phút)

### Tính năng
- **Giữ chỗ 10 phút**: User đăng nhập có thể reserve booth trước, hold 10 phút
- **Venue page**: Trang danh sách booth (`?page=venue`) hiển thị trạng thái real-time
- **Block walk-in**: Booth đã đặt → user khác không bấm Start được
- **Auto-expire**: Reservation tự hết hạn sau 10 phút
- **Atomic reservation**: RPC function `create_reservation()` xử lý race condition
- **Reservation banner**: Booth page hiện banner khi có reservation (mine/blocked)
- **Real-time updates**: Supabase Realtime cho cả venue page và booth page

### Database
- Bảng `reservations` mới (id, booth_id, user_id, status, created_at, expires_at)
- UNIQUE partial indexes: 1 active reservation/booth, 1 active reservation/user
- RPC `create_reservation()`: atomic check + insert, auto-expire
- RPC `cancel_reservation()`: chỉ owner mới hủy được
- Function `expire_reservations()`: cleanup hết hạn

### Constraints
- Bắt buộc đăng nhập để đặt chỗ
- Mỗi user chỉ 1 reservation active
- Mỗi booth chỉ 1 reservation active
- Không đặt được booth đang dùng (active session)

### File thay đổi
- `index.html` (venue page HTML/CSS/JS, reservation banner, handleStart check)
- `migration_reservations.sql` (NEW - chạy trong Supabase SQL Editor)

---

## [2026-03-14] Lazy Registration + Merged Post-Session UX

### Tinh nang
- **Lazy Registration**: User dùng booth anonymous trước, đăng ký ngay trong completion overlay
- **Phone + Password auth**: Đăng ký/đăng nhập bằng SĐT + mật khẩu, bcrypt hash server-side
- **Session linking**: Session tự động gắn user_id khi đã đăng nhập, hoặc link sau khi đăng ký
- **Device data migration**: Khi đăng ký, product_impressions/leads từ device ID chuyển sang real user ID
- **Landing page login**: Link "Đăng nhập" trên landing page hoạt động
- **Merged post-session overlay**: Gộp completion + feedback + registration thành 1 overlay scrollable (giảm từ 4-5 tap xuống 2 tap)
- **Feedback optional**: Không bắt buộc rate đủ 3 tiêu chí, auto-send khi bấm "Tắt máy"
- **Auto-login**: Returning user tự động nhận diện qua localStorage token

### Database
- Bảng `users` mới (id, phone, password_hash, name, created_at, last_login)
- Cột `user_id` (nullable) thêm vào bảng `sessions`
- RPC functions `register_user()` và `login_user()` (SECURITY DEFINER, bcrypt)
- `REVOKE SELECT (password_hash)` bảo vệ hash khỏi anon key

### Flow
```
Session kết thúc → Feedback → isLoggedIn?
  NO  → Registration prompt (Đăng ký / Đăng nhập / Để sau)
  YES → Product Discovery (như cũ)
```

### File thay doi
- `index.html` (auth overlay, CSS, JS logic)
- `migration_users.sql` (NEW - chạy trong Supabase SQL Editor)

---

## [2026-03-13] Latency Audit & Stability Fix - FW v1.0.1

### Bug fix
- **ESP32 HTTP calls block relay**: `createSession()`, `endSession()`, `pauseSession()`, `resumeSession()`, `markExecuted()` chạy blocking trong lúc relay đang mở → relay timing bị lệch, loop bị block 5-9s. **Fix**: Deferred task system — set flag, chạy HTTP ở loop iteration tiếp theo
- **Heartbeat + Poll stack cùng cycle**: Mỗi 5s heartbeat (3s) + poll (3s) chạy tuần tự = 6s block, ws.loop() không được gọi → command qua WebSocket bị delay 10-20s. **Fix**: Deferred tasks ưu tiên trước heartbeat, heartbeat skip khi có pending task
- **OTA check quá thường xuyên**: `checkOtaUpdates()` chạy mỗi 5s với timeout 5s → thêm 5s block. **Fix**: Giãn ra 1 giờ/lần, giảm timeout xuống 3s
- **NTP retry block 500ms mỗi loop**: `getLocalTime(500ms)` chạy mỗi iteration cho đến khi sync. **Fix**: Giảm timeout xuống 50ms
- **Frontend latency tracking overlap**: `setInterval(300ms)` tạo request chồng chất khi mạng chậm. **Fix**: Đổi sang `setTimeout` chaining — chờ xong mới poll tiếp
- **Button disabled vĩnh viễn khi timeout**: User bị kẹt không bấm được gì sau 10s timeout. **Fix**: Re-enable tất cả buttons khi timeout
- **Frontend pollState overlap**: `setInterval(pollState, 3s)` chồng chất khi query chậm. **Fix**: Đổi sang `setTimeout` chaining
- **handleStart 3 query tuần tự**: Mất 300-900ms trước khi insert command. **Fix**: Chạy 2 validation song song `Promise.all`
- **Admin 14 query tuần tự mỗi 5s**: Gây chậm + overlap. **Fix**: Song song `Promise.all`, data ít thay đổi chỉ load mỗi 30s, mutex flag chống overlap
- **Admin sendCommand nuốt lỗi**: Supabase trả error nhưng vẫn hiện "Đã gửi". **Fix**: Check `{ error }` + throw

### Thay doi
- ESP32: Deferred task queue (pendingCreateSession, pendingEndSession, pendingMarkExecuted...)
- ESP32: OTA check interval 5s → 3,600s (1 giờ)
- ESP32: NTP retry timeout 500ms → 50ms
- ESP32: Firmware version 1.0.0 → 1.0.1
- Frontend: `startLatencyTracking()` dùng setTimeout + re-enable buttons on timeout
- Frontend: `handleStart()` dùng Promise.all cho validation
- Frontend: pollState dùng setTimeout chaining
- Admin: refresh() song song + tách slow data (30s) + mutex
- Admin: sendCommand() check error

### File thay doi
- `booth_esp32_3/booth_esp32_3.ino`, `index.html`, `admin.html`

---

## [2026-03-13] OTA Firmware Update + Admin Responsive + Logitech Seed

### Tinh nang
- **OTA firmware update**: ESP32 poll ota_updates table mỗi 1 giờ, tải .bin qua HTTP, flash, reboot
- **Admin Firmware tab**: Upload firmware, quản lý version, trigger OTA per-booth hoặc bulk, OTA history
- **Admin responsive**: Mobile-friendly CSS với breakpoint 768px (tablet) và 480px (mobile)
- **Logitech seed data**: Brand Logitech + MX Master 3S + MX Keys S, gán vào BOOTH_001 và BOOTH_002
- **Heartbeat gửi firmware_version**: Admin thấy FW version mỗi booth

### Database
- Bảng mới: `firmwares`, `ota_updates`
- Cột mới trên `booths`: `firmware_version`, `ota_status`, `ota_progress`

### Yeu cau
- Chạy `migration_ota.sql` trong Supabase SQL Editor
- Tạo Supabase Storage bucket "firmware" (Public)
- ESP32 cần partition scheme: Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)

### File thay doi
- `booth_esp32_3/booth_esp32_3.ino`, `admin.html`, `migration_ota.sql`, `seed_products.sql`

---

## [2026-03-13] Supabase Realtime - Giảm latency mở cửa ~2.5x

### Tinh nang
- **WebSocket Realtime**: ESP32 nhận command qua Supabase Realtime (WebSocket) thay vì chỉ poll HTTP. Latency giảm từ ~700ms xuống ~200-300ms
- **Hybrid mode**: WebSocket là primary, HTTP poll mỗi 5s là backup. Nếu WS mất → tự fallback về poll 500ms
- **Phoenix protocol**: Join channel, heartbeat 25s, auto-reconnect khi WiFi mất/lên lại
- **Dedup**: Cùng command không bị xử lý 2 lần giữa WS và HTTP poll

### Thay doi
- Thêm `WebSocketsClient.h` (cần cài library "WebSockets" by Markus Sattler)
- `connectRealtime()`, `wsEvent()`, `wsJoinChannel()`, `wsPhoenixHeartbeat()`, `handleRealtimeMessage()`
- `executeCommand()` - shared logic cho cả WS và poll
- `POLL_FALLBACK_MS = 5000` khi WS hoạt động, `POLL_INTERVAL_MS = 500` khi WS mất

### Yeu cau
- Cài library: Arduino IDE → Library Manager → "WebSockets" by Markus Sattler
- Chạy SQL: `ALTER PUBLICATION supabase_realtime ADD TABLE commands;`

### File thay doi
- `booth_esp32_3/booth_esp32_3.ino`, `migration_realtime.sql`

---

## [2026-03-13] Command Latency Tracking

### Tinh nang
- **Đo latency realtime**: User thấy "Đang mở cửa... 1.2s" → "Cửa đã mở! (1.8s)" sau khi bấm
- **ESP32 ghi timestamp**: `executed_at` (thời điểm nhận command), `relay_at` (thời điểm mở relay)
- **Relay trước, DB sau**: ESP32 mở relay ngay khi nhận command, ghi DB sau → giảm ~300ms
- **Tách heartbeat**: HTTP heartbeat mỗi 5s riêng, không chặn poll commands
- **Admin Latency Dashboard**: Avg/Min/Max latency, filter theo booth/action/ngày, bảng chi tiết với màu xanh/vàng/đỏ
- **Timeout 10s**: Web app tự timeout nếu booth không phản hồi

### Thay doi
- `commands` table: thêm cột `executed_at`, `relay_at` (timestamptz)
- ESP32: poll 2s → 500ms, HTTP timeout 5s → 3s, `markExecutedWithTimestamp()`
- Web: `startLatencyTracking()` cho tất cả actions (open/close/pause/resume)
- Admin: section "Command Latency" trong tab Reports

### Yeu cau
- Chạy `migration_latency.sql` trong Supabase SQL Editor

### File thay doi
- `booth_esp32_3/booth_esp32_3.ino`, `index.html`, `admin.html`, `migration_latency.sql`

---

## [2026-03-12] Product Placement Phase 1 (MVP)

### Tinh nang
- **3 Brands, 4 Products seed data**: Highlands Coffee, The Coffee House, Epione
- **Admin CRUD**: Tab navigation, Brand/Product/Booth Product management, QR code generation
- **Post-session Product Discovery**: Hiển thị max 2 sản phẩm sau khi kết thúc phiên (Brand trước Epione)
- **Product Detail**: Full-screen overlay với image carousel, swipe, thông tin sản phẩm
- **Lead Capture**: Voucher CTA (copy mã) hoặc "Tôi quan tâm", dedup cùng ngày
- **Check-in Products**: Hiển thị sản phẩm khi booth idle
- **QR Deep Link**: Scan QR → mở thẳng product detail
- **Report Dashboard**: Impressions, Leads, Vouchers, Conversion rate, export CSV
- **Device User ID**: `crypto.randomUUID()` + localStorage (không cần auth cho MVP)
- **Impression dedup**: Cùng user + sản phẩm + source chỉ đếm 1 lần/session

### Database
- 5 bảng mới: `brands`, `products`, `booth_products`, `product_impressions`, `product_leads`
- RLS "allow all" cho MVP

### File thay doi
- `index.html`, `admin.html`, `migration_products.sql`, `seed_products.sql`

---

## [2026-03-12] WiFi logging cho admin giám sát

### Tinh nang
- **ESP32 log WiFi events lên Supabase**: `wifi_reconnect` (kèm downtime, retry count, RSSI), `wifi_weak_signal` (RSSI < -75 dBm, cảnh báo mỗi 5 phút)
- **Admin dashboard - section "WiFi Log"**: Hiển thị riêng sự cố WiFi, filter theo booth, badge màu (xanh = reconnect OK, vàng = tín hiệu yếu)

### File thay doi
- `booth_esp32_3/booth_esp32_3.ino`, `admin.html`

---

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
