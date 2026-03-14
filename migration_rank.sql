-- ============================================================
-- Migration: Rank System (Hệ thống thăng hạng)
-- Chạy trong Supabase SQL Editor
-- ============================================================

-- 1. Thêm cột rank profile vào users
ALTER TABLE users ADD COLUMN IF NOT EXISTS nickname text;
ALTER TABLE users ADD COLUMN IF NOT EXISTS avatar_icon text DEFAULT '🌱';
ALTER TABLE users ADD COLUMN IF NOT EXISTS show_leaderboard boolean DEFAULT true;

-- Index cho leaderboard query
CREATE INDEX IF NOT EXISTS idx_users_show_leaderboard ON users (show_leaderboard) WHERE show_leaderboard = true;

-- 2. Update login_user() để trả thêm rank fields
CREATE OR REPLACE FUNCTION login_user(p_phone text, p_password text)
RETURNS json
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
  v_user users%ROWTYPE;
BEGIN
  SELECT * INTO v_user FROM users
  WHERE phone = p_phone
    AND password_hash = crypt(p_password, password_hash);

  IF NOT FOUND THEN
    RETURN json_build_object('error', 'INVALID_CREDENTIALS');
  END IF;

  UPDATE users SET last_login = now() WHERE id = v_user.id;

  RETURN json_build_object(
    'id', v_user.id,
    'phone', v_user.phone,
    'name', v_user.name,
    'occupation', v_user.occupation,
    'purpose', v_user.purpose,
    'nickname', v_user.nickname,
    'avatar_icon', v_user.avatar_icon,
    'show_leaderboard', v_user.show_leaderboard,
    'created_at', v_user.created_at
  );
END;
$$;

-- 3. Update register_user() để trả thêm rank fields
CREATE OR REPLACE FUNCTION register_user(p_phone text, p_password text)
RETURNS json
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
  v_user users%ROWTYPE;
BEGIN
  IF EXISTS (SELECT 1 FROM users WHERE phone = p_phone) THEN
    RETURN json_build_object('error', 'PHONE_EXISTS');
  END IF;

  INSERT INTO users (phone, password_hash)
  VALUES (p_phone, crypt(p_password, gen_salt('bf')))
  RETURNING * INTO v_user;

  RETURN json_build_object(
    'id', v_user.id,
    'phone', v_user.phone,
    'name', v_user.name,
    'occupation', v_user.occupation,
    'purpose', v_user.purpose,
    'nickname', v_user.nickname,
    'avatar_icon', v_user.avatar_icon,
    'show_leaderboard', v_user.show_leaderboard,
    'created_at', v_user.created_at
  );
END;
$$;

-- 4. RPC: Lấy leaderboard theo location
CREATE OR REPLACE FUNCTION get_leaderboard(p_location text DEFAULT NULL)
RETURNS json
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
  v_result json;
BEGIN
  SELECT json_agg(row_to_json(t)) INTO v_result
  FROM (
    SELECT
      u.id,
      COALESCE(u.nickname, 'Ẩn danh') as nickname,
      COALESCE(u.avatar_icon, '🌱') as avatar_icon,
      COALESCE(SUM(s.duration_seconds / 60), 0)::int as total_minutes
    FROM users u
    INNER JOIN sessions s ON s.user_id = u.id::text AND s.status = 'ended'
    WHERE u.show_leaderboard = true
      AND (p_location IS NULL OR s.booth_id IN (
        SELECT booth_id FROM booths WHERE location = p_location
      ))
    GROUP BY u.id, u.nickname, u.avatar_icon
    HAVING SUM(s.duration_seconds) > 0
    ORDER BY total_minutes DESC
    LIMIT 50
  ) t;

  RETURN COALESCE(v_result, '[]'::json);
END;
$$;
