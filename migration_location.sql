-- ============================================================
-- Migration: Thêm location cho booths + quản lý đa địa điểm
-- ============================================================

-- Thêm cột location và address vào bảng booths
ALTER TABLE booths ADD COLUMN IF NOT EXISTS location TEXT DEFAULT '';
ALTER TABLE booths ADD COLUMN IF NOT EXISTS address TEXT DEFAULT '';

-- Cập nhật booth mẫu (tuỳ chỉnh theo địa điểm thật)
-- UPDATE booths SET location = 'Quận 1', address = '123 Nguyễn Huệ' WHERE booth_id = 'BOOTH_001';
-- UPDATE booths SET location = 'Quận 7', address = '456 Nguyễn Văn Linh' WHERE booth_id = 'BOOTH_002';
