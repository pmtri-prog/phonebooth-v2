# Plan: B2B Booth Rental Promotion trong User App

**Created:** 2026-03-15
**Status:** Draft
**Complexity:** Medium
**Effort:** ~3-4h

## Bối cảnh

Epione có dịch vụ cho thuê booth vật lý đặt tại văn phòng doanh nghiệp (B2B, thu phí theo tháng). User hiện tại dùng booth tại các venue coworking — đây chính là đối tượng tiềm năng nhất để mang booth về văn phòng riêng.

**Chiến lược:** Product-led growth — user yêu thích trải nghiệm booth → gợi ý mang về văn phòng → thu lead B2B.

## Nguyên tắc thiết kế

- **Contextual, not interruptive** — hiện đúng lúc user đang hài lòng
- **Vừa phải** — CTA rõ ràng nhưng không chiếm diện tích lớn, không popup
- **Target đúng** — chỉ hiện cho user đã dùng booth (không hiện trên landing cho visitor lần đầu)
- **Không ảnh hưởng flow chính** — không block bất kỳ hành động nào của user

## Phân tích touchpoint

| Thời điểm | Tâm lý user | Phù hợp? | Lý do |
|---|---|---|---|
| Completion overlay (sau phiên) | Hài lòng, vừa deep work xong | **Cao nhất** | User đang cảm nhận rõ giá trị booth |
| Profile page | Xem lại thành tích | **Cao** | User trung thành, dùng nhiều lần |
| Venue page footer | Đang chọn booth | Trung bình | Đang bận tìm booth, chưa muốn bị distract |
| Landing page | Mới vào app | Thấp | Chưa trải nghiệm, chưa thấy giá trị |

---

## Phase 1: Completion Overlay — "Mang booth về văn phòng"

**Vị trí:** Sau feedback section, trước nút "Tắt máy" trong completion overlay.
**Điều kiện hiện:** User đã hoàn thành ít nhất 2 phiên (check session count).

### UI Design

```
┌─────────────────────────────────┐
│  (completion content hiện tại)  │
│  ... feedback stars ...         │
│  ... rank card ...              │
│                                 │
│  ┌─────────────────────────────┐│
│  │ 🏢  Muốn có booth riêng    ││
│  │     tại văn phòng?          ││
│  │                             ││
│  │  Thuê booth Epione cho team ││
│  │  — cách âm, smart lock,    ││
│  │  quản lý qua app.          ││
│  │                             ││
│  │  [Tìm hiểu thêm →]        ││
│  └─────────────────────────────┘│
│                                 │
│  [ Tắt máy ]                   │
└─────────────────────────────────┘
```

### Styling
- Card nhỏ, border `var(--color-border)`, border-radius `var(--radius-card)`
- Background: `#F8F8FF` (hơi tím nhạt, subtle)
- Font size nhỏ hơn main content (13px body, 15px title)
- Icon 🏢 nhỏ, inline với title
- CTA "Tìm hiểu thêm →" dạng text link (không phải button lớn), color accent
- Không có close/dismiss button (nó chỉ là 1 card nhỏ trong scroll)

### Logic
```js
// Chỉ hiện khi user đã dùng >= 2 phiên
// Check bằng session count trong DB hoặc localStorage counter
const sessionCount = parseInt(localStorage.getItem('epione_session_count') || '0');
if (sessionCount >= 2) {
  document.getElementById('boothRentalPromo').classList.remove('hidden');
}

// Increment counter khi session kết thúc
function onSessionEnd() {
  const count = parseInt(localStorage.getItem('epione_session_count') || '0') + 1;
  localStorage.setItem('epione_session_count', count.toString());
}
```

### CTA Action
Click "Tìm hiểu thêm →" sẽ:
1. Track event (insert vào `booth_logs` hoặc table mới `rental_leads`)
2. Mở trang rental info — 2 options:
   - **Option A:** Link đến 1 trang riêng `rental.html` (tương tự partner.html)
   - **Option B:** Mở Zalo/Messenger chat trực tiếp
   - **Option C (recommended):** Bottom sheet ngay trong app với thông tin ngắn + form liên hệ đơn giản (tên, SĐT, công ty)

---

## Phase 2: Profile Page — Subtle CTA cho loyal user

**Vị trí:** Sau section "Voucher của tôi", trước nút "Đăng xuất".
**Điều kiện:** User đã login VÀ có ít nhất tier Bạc (>= 2h deep work).

### UI Design

```
┌─────────────────────────────────┐
│  Voucher của tôi                │
│  ...                            │
│                                 │
│  ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─   │
│  🏢 Booth cho văn phòng        │
│  Đặt booth Epione tại công ty  │
│  để team luôn có không gian     │
│  riêng tư.  Tìm hiểu →        │
│  ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─   │
│                                 │
│  [ Đăng xuất ]                  │
└─────────────────────────────────┘
```

### Styling
- Dạng inline text block, không phải card nổi bật
- Font 13px, color `var(--color-text-secondary)`
- "Tìm hiểu →" color accent, font-weight 600
- Padding nhẹ, dấu gạch ngang nhạt bao quanh

---

## Phase 3: Landing Page Footer — Thêm link "Thuê booth"

**Vị trí:** Trong `.landing-partner-links`, thêm link thứ 3.
**Không cần điều kiện** — ai cũng thấy, nhưng vị trí rất nhỏ.

### UI
```
Venue Partner | Advertiser | Thuê booth
```

### Implementation
Thêm 1 dòng HTML vào footer links:
```html
<span class="sep">|</span>
<a href="rental.html">Thuê booth</a>
```

---

## Phase 4: Trang Rental Info (rental.html hoặc bottom sheet)

### Option đề xuất: Bottom sheet trong app + trang rental.html

**Bottom sheet (trong completion/profile CTA):**
```
┌─────────────────────────────────┐
│  ╌╌╌ (drag handle) ╌╌╌         │
│                                 │
│  🏢 Đặt Epione Booth           │
│     tại văn phòng               │
│                                 │
│  ✓ Cách âm cao cấp             │
│  ✓ Smart lock, quản lý qua app │
│  ✓ Lắp đặt trong 1 ngày        │
│  ✓ Thuê theo tháng, linh hoạt  │
│                                 │
│  ── Để lại thông tin ──         │
│                                 │
│  Tên: [___________________]    │
│  SĐT: [___________________]    │
│  Công ty: [________________]   │
│                                 │
│  [ Gửi yêu cầu ]               │
│                                 │
│  Hoặc nhắn Zalo: 0909.xxx.xxx  │
└─────────────────────────────────┘
```

**Trang rental.html (cho landing page link):**
- Tương tự partner.html về layout
- Hero section + 3-4 selling points + form liên hệ
- Có thể làm sau, MVP dùng bottom sheet trước

### Database: rental_leads table

```sql
CREATE TABLE rental_leads (
  id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
  name TEXT,
  phone TEXT NOT NULL,
  company TEXT,
  source TEXT DEFAULT 'app', -- 'completion', 'profile', 'landing'
  user_id UUID,
  device_id TEXT,
  session_count INT,
  created_at TIMESTAMPTZ DEFAULT NOW()
);

-- RLS: allow insert from anon
ALTER TABLE rental_leads ENABLE ROW LEVEL SECURITY;
CREATE POLICY "Allow insert rental leads" ON rental_leads FOR INSERT WITH CHECK (true);
```

---

## Tổng kết implementation

| Phase | Effort | Priority | Impact |
|---|---|---|---|
| 1. Completion overlay promo | ~1.5h | P0 | Cao - đúng moment, đúng đối tượng |
| 2. Profile page CTA | ~30min | P1 | Trung bình - loyal users |
| 3. Landing footer link | ~5min | P2 | Thấp - nhưng zero-effort |
| 4. Bottom sheet + rental_leads | ~1.5h | P0 | Cao - capture lead |

**Thứ tự làm:** Phase 4 (DB + bottom sheet) → Phase 1 → Phase 3 → Phase 2

## File changes

| File | Changes |
|---|---|
| `index.html` | HTML: promo card trong completion, profile section, landing footer link, bottom sheet overlay. CSS: styles cho promo card + bottom sheet. JS: session counter, show/hide logic, form submit |
| `migration_rental_leads.sql` | Tạo table rental_leads |

## Risks & Mitigations

| Risk | Mitigation |
|---|---|
| User thấy annoying | Chỉ hiện sau 2+ phiên, styling subtle, không popup/modal |
| Form không ai điền | Thêm option Zalo/Messenger trực tiếp |
| Spam leads | Rate limit bằng device_id (1 lead/device/ngày) |
