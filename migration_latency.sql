-- ============================================================
-- Latency Tracking - Migration
-- Thêm cột để đo thời gian từ user bấm → cửa mở thực tế
-- Chạy trong Supabase SQL Editor
-- ============================================================

-- Thêm cột vào commands
ALTER TABLE commands ADD COLUMN IF NOT EXISTS executed_at timestamptz;
ALTER TABLE commands ADD COLUMN IF NOT EXISTS relay_at timestamptz;

-- executed_at: thời điểm ESP32 nhận command (trước khi mở relay)
-- relay_at:    thời điểm ESP32 bật relay (cửa mở thực tế)
-- Latency = relay_at - created_at

-- Index cho query latency report
CREATE INDEX IF NOT EXISTS idx_commands_latency ON commands (booth_id, created_at)
  WHERE status = 'executed' AND relay_at IS NOT NULL;
