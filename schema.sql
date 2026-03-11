-- ============================================================
-- Booth Door Control System - Supabase Schema
-- ============================================================

-- Xóa bảng cũ (nếu có)
DROP TABLE IF EXISTS commands CASCADE;
DROP TABLE IF EXISTS sessions CASCADE;
DROP TABLE IF EXISTS booth_logs CASCADE;

-- Bảng commands: lệnh từ web gửi xuống ESP32
CREATE TABLE commands (
  id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  booth_id    text NOT NULL,
  action      text NOT NULL CHECK (action IN ('open', 'close', 'pause', 'resume')),
  status      text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'executed')),
  created_at  timestamptz NOT NULL DEFAULT now()
);

-- Bảng sessions: theo dõi phiên sử dụng booth
CREATE TABLE sessions (
  id                uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  booth_id          text NOT NULL,
  started_at        timestamptz NOT NULL DEFAULT now(),
  ended_at          timestamptz,
  duration_seconds      int,
  status                text NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'paused', 'ended')),
  paused_at             timestamptz,
  total_paused_seconds  int NOT NULL DEFAULT 0,
  pause_count           int NOT NULL DEFAULT 0
);

-- Index để ESP32 poll nhanh
CREATE INDEX idx_commands_pending ON commands (booth_id, status, created_at)
  WHERE status = 'pending';

-- Index để tìm session active/paused nhanh
CREATE INDEX idx_sessions_active ON sessions (booth_id, status)
  WHERE status IN ('active', 'paused');

-- Bảng booth_logs: ghi lại sự kiện đặc biệt (emergency, etc.)
CREATE TABLE booth_logs (
  id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  booth_id    text NOT NULL,
  event       text NOT NULL,
  note        text,
  created_at  timestamptz NOT NULL DEFAULT now()
);

-- Bảng booths: heartbeat để biết booth online/offline
CREATE TABLE booths (
  booth_id    text PRIMARY KEY,
  name        text NOT NULL DEFAULT '',
  last_seen   timestamptz NOT NULL DEFAULT now()
);

-- Tạo sẵn booth mặc định
INSERT INTO booths (booth_id, name) VALUES ('BOOTH_001', 'PhoneBooth 1');
INSERT INTO booths (booth_id, name) VALUES ('BOOTH_002', 'PhoneBooth 2');

-- RLS: cho phép anon key đọc/ghi (MVP - không cần auth)
ALTER TABLE commands ENABLE ROW LEVEL SECURITY;
ALTER TABLE sessions ENABLE ROW LEVEL SECURITY;

CREATE POLICY "Allow all for commands" ON commands
  FOR ALL USING (true) WITH CHECK (true);

CREATE POLICY "Allow all for sessions" ON sessions
  FOR ALL USING (true) WITH CHECK (true);

ALTER TABLE booth_logs ENABLE ROW LEVEL SECURITY;
ALTER TABLE booths ENABLE ROW LEVEL SECURITY;

CREATE POLICY "Allow all for booth_logs" ON booth_logs
  FOR ALL USING (true) WITH CHECK (true);

CREATE POLICY "Allow all for booths" ON booths
  FOR ALL USING (true) WITH CHECK (true);
