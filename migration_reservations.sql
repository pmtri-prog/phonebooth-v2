-- ============================================================
-- Migration: Booth Reservation (Giữ chỗ 10 phút)
-- Chạy trong Supabase SQL Editor
-- ============================================================

-- 1. Bảng reservations
CREATE TABLE reservations (
  id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  booth_id    text NOT NULL REFERENCES booths(booth_id),
  user_id     uuid NOT NULL REFERENCES users(id),
  status      text NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'used', 'expired', 'cancelled')),
  created_at  timestamptz NOT NULL DEFAULT now(),
  expires_at  timestamptz NOT NULL DEFAULT (now() + interval '10 minutes')
);

-- Mỗi booth chỉ 1 reservation active
CREATE UNIQUE INDEX idx_one_active_per_booth
  ON reservations (booth_id) WHERE status = 'active';

-- Mỗi user chỉ 1 reservation active
CREATE UNIQUE INDEX idx_one_active_per_user
  ON reservations (user_id) WHERE status = 'active';

-- Index cho query theo status
CREATE INDEX idx_reservations_status ON reservations (status, expires_at)
  WHERE status = 'active';

-- RLS
ALTER TABLE reservations ENABLE ROW LEVEL SECURITY;
CREATE POLICY "Allow all for reservations" ON reservations
  FOR ALL USING (true) WITH CHECK (true);

-- 2. Function: expire hết hạn
CREATE OR REPLACE FUNCTION expire_reservations()
RETURNS void
LANGUAGE plpgsql
AS $$
BEGIN
  UPDATE reservations
  SET status = 'expired'
  WHERE status = 'active' AND expires_at < now();
END;
$$;

-- 3. RPC: tạo reservation (atomic, race-safe)
CREATE OR REPLACE FUNCTION create_reservation(p_booth_id text, p_user_id uuid)
RETURNS json
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
  v_resv reservations%ROWTYPE;
BEGIN
  -- Auto-expire hết hạn trước
  PERFORM expire_reservations();

  -- Check booth có session active/paused không
  IF EXISTS (
    SELECT 1 FROM sessions
    WHERE booth_id = p_booth_id AND status IN ('active', 'paused')
  ) THEN
    RETURN json_build_object('error', 'BOOTH_OCCUPIED');
  END IF;

  -- Check user đã có reservation active không
  IF EXISTS (
    SELECT 1 FROM reservations
    WHERE user_id = p_user_id AND status = 'active'
  ) THEN
    RETURN json_build_object('error', 'USER_HAS_RESERVATION');
  END IF;

  -- Check booth đã có reservation active không
  IF EXISTS (
    SELECT 1 FROM reservations
    WHERE booth_id = p_booth_id AND status = 'active'
  ) THEN
    RETURN json_build_object('error', 'BOOTH_RESERVED');
  END IF;

  -- Insert reservation
  INSERT INTO reservations (booth_id, user_id)
  VALUES (p_booth_id, p_user_id)
  RETURNING * INTO v_resv;

  RETURN json_build_object(
    'id', v_resv.id,
    'booth_id', v_resv.booth_id,
    'user_id', v_resv.user_id,
    'status', v_resv.status,
    'created_at', v_resv.created_at,
    'expires_at', v_resv.expires_at
  );
END;
$$;

-- 4. RPC: hủy reservation (chỉ owner)
CREATE OR REPLACE FUNCTION cancel_reservation(p_reservation_id uuid, p_user_id uuid)
RETURNS json
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
  v_resv reservations%ROWTYPE;
BEGIN
  SELECT * INTO v_resv FROM reservations
  WHERE id = p_reservation_id AND user_id = p_user_id AND status = 'active';

  IF NOT FOUND THEN
    RETURN json_build_object('error', 'NOT_FOUND');
  END IF;

  UPDATE reservations SET status = 'cancelled' WHERE id = p_reservation_id;

  RETURN json_build_object('success', true);
END;
$$;
