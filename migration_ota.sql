-- ============================================================
-- OTA Firmware Update - Migration
-- Quản lý firmware version và cập nhật từ xa qua WiFi
-- Chạy trong Supabase SQL Editor
-- ============================================================

-- 1. Thêm cột firmware_version vào booths
ALTER TABLE booths ADD COLUMN IF NOT EXISTS firmware_version text NOT NULL DEFAULT '';
ALTER TABLE booths ADD COLUMN IF NOT EXISTS ota_status text NOT NULL DEFAULT ''
  CHECK (ota_status IN ('', 'pending', 'downloading', 'success', 'failed'));
ALTER TABLE booths ADD COLUMN IF NOT EXISTS ota_progress int NOT NULL DEFAULT 0;

-- 2. Bảng firmwares: lưu các bản firmware đã upload
CREATE TABLE IF NOT EXISTS firmwares (
  id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  version     text NOT NULL UNIQUE,
  file_url    text NOT NULL,
  file_size   int NOT NULL DEFAULT 0,
  changelog   text NOT NULL DEFAULT '',
  is_latest   boolean NOT NULL DEFAULT false,
  created_at  timestamptz NOT NULL DEFAULT now()
);

-- 3. Bảng ota_updates: lịch sử cập nhật firmware
CREATE TABLE IF NOT EXISTS ota_updates (
  id            uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  booth_id      text NOT NULL REFERENCES booths(booth_id),
  firmware_id   uuid NOT NULL REFERENCES firmwares(id) ON DELETE CASCADE,
  status        text NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'downloading', 'success', 'failed')),
  error_message text DEFAULT '',
  started_at    timestamptz NOT NULL DEFAULT now(),
  completed_at  timestamptz
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_ota_updates_booth ON ota_updates (booth_id, started_at DESC);
CREATE INDEX IF NOT EXISTS idx_firmwares_latest ON firmwares (is_latest) WHERE is_latest = true;

-- RLS
ALTER TABLE firmwares ENABLE ROW LEVEL SECURITY;
ALTER TABLE ota_updates ENABLE ROW LEVEL SECURITY;

DO $$ BEGIN
  IF NOT EXISTS (SELECT 1 FROM pg_policies WHERE tablename = 'firmwares' AND policyname = 'Allow all for firmwares') THEN
    CREATE POLICY "Allow all for firmwares" ON firmwares FOR ALL USING (true) WITH CHECK (true);
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_policies WHERE tablename = 'ota_updates' AND policyname = 'Allow all for ota_updates') THEN
    CREATE POLICY "Allow all for ota_updates" ON ota_updates FOR ALL USING (true) WITH CHECK (true);
  END IF;
END $$;

-- 4. Bật Realtime cho ota_updates (để admin thấy progress realtime)
ALTER PUBLICATION supabase_realtime ADD TABLE ota_updates;
