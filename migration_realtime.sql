-- ============================================================
-- Enable Supabase Realtime cho bảng commands
-- Chạy trong Supabase SQL Editor
-- ============================================================

-- Bật Realtime cho bảng commands (bắt buộc để WebSocket nhận INSERT events)
ALTER PUBLICATION supabase_realtime ADD TABLE commands;
