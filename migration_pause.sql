-- ============================================================
-- Migration: Thêm tính năng Tạm dừng / Tiếp tục (Pause/Resume)
-- Chạy script này trên Supabase SQL Editor
-- ============================================================

-- 1. Thêm cột mới vào bảng sessions
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS paused_at timestamptz;
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS total_paused_seconds int NOT NULL DEFAULT 0;
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS pause_count int NOT NULL DEFAULT 0;

-- 2. Mở rộng status constraint cho sessions
ALTER TABLE sessions DROP CONSTRAINT IF EXISTS sessions_status_check;
ALTER TABLE sessions ADD CONSTRAINT sessions_status_check
  CHECK (status IN ('active', 'paused', 'ended'));

-- 3. Mở rộng action constraint cho commands
ALTER TABLE commands DROP CONSTRAINT IF EXISTS commands_action_check;
ALTER TABLE commands ADD CONSTRAINT commands_action_check
  CHECK (action IN ('open', 'close', 'pause', 'resume'));

-- 4. Cập nhật index để bao gồm cả paused
DROP INDEX IF EXISTS idx_sessions_active;
CREATE INDEX idx_sessions_active ON sessions (booth_id, status)
  WHERE status IN ('active', 'paused');
