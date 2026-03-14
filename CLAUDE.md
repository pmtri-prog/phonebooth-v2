# CLAUDE.md - Epione PhoneBooth

## Project Overview
Epione PhoneBooth - hệ thống quản lý buồng làm việc riêng tư (phonebooth) cho coworking space.
ESP32 điều khiển khóa cửa, web app cho user đặt/dùng booth, admin dashboard quản lý.

## Tech Stack
- **Frontend**: Vanilla HTML/CSS/JS (single file), hosted on Vercel
- **Backend**: Supabase (PostgreSQL + Realtime + Storage)
- **Hardware**: ESP32 + Relay module + magnetic lock
- **Language**: Vietnamese UI, code comments mixed Vietnamese/English

## Architecture
```
User App (index.html) → Supabase commands table → ESP32 WebSocket/Poll → Relay
Admin (admin.html) → Supabase direct queries
ESP32 → Supabase REST API (heartbeat, sessions, OTA)
```

## Key Files
| File | Purpose |
|------|---------|
| `index.html` | User-facing web app (booth control, product discovery, feedback) |
| `admin.html` | Admin dashboard (booth management, reports, firmware, products) |
| `booth_esp32_3/booth_esp32_3.ino` | ESP32 firmware (relay, WiFi, WebSocket, OTA) |
| `migration_*.sql` | Database migrations (run in Supabase SQL Editor) |
| `seed_products.sql` | Product placement seed data |
| `CHANGELOG.md` | Detailed change history |

## Database Tables
- `booths` - Booth registry + heartbeat + firmware version
- `sessions` - User sessions (active/paused/ended)
- `commands` - Remote control commands (open/close/pause/resume)
- `booth_logs` - Event logging
- `brands`, `products`, `booth_products` - Product placement
- `product_impressions`, `product_leads` - Ad tracking
- `firmwares`, `ota_updates` - OTA firmware management

## ESP32 Critical Patterns
- **Deferred HTTP tasks**: Never block relay operations with HTTP calls. Use flags (pendingCreateSession, etc.) and process in next loop iteration
- **One HTTP call per loop**: Don't stack heartbeat + poll + OTA in same iteration
- **Guard OTA**: Skip OTA check during active session or unlock
- **WebSocket primary, HTTP poll backup**: WS for instant commands, poll every 5s as fallback

## Frontend Patterns
- **Supabase Realtime + Poll backup**: Subscribe to sessions/commands changes for instant UI, poll every 5s as backup
- **setTimeout chaining**: Never use setInterval for async operations (causes overlap)
- **Button re-enable on timeout**: Always re-enable buttons after 10s timeout
- **Promise.all for validation**: Run independent checks in parallel

## Deploy Flow
1. `git push` → Vercel auto-deploys index.html + admin.html
2. ESP32: Arduino IDE → Export Compiled Binary → Upload .bin to Supabase Storage → Trigger OTA from admin
3. Database: Run migration SQL files in Supabase SQL Editor

## Design System
- Font: Nunito Sans (400/500/600/700)
- Primary: #10069F (accent), #282828 (black), #EDEDED (gray)
- Radius: 24px buttons, 12px cards, 16px sheets, 8px inputs
- CSS Variables: --color-*, --radius-*, --space-*

## Supabase Config
- Project URL: https://iwczjfomyybszwdlzwjs.supabase.co
- Realtime enabled: commands, sessions, ota_updates
- Storage bucket: "firmware" (public)
- RLS: "Allow all" policies (MVP)

## Conventions
- Commits: descriptive message in English, Co-Authored-By Claude
- CHANGELOG.md: update with every feature/bugfix
- No build step: vanilla HTML served directly
- ESP32 firmware version: increment on every change (currently v1.0.1)
