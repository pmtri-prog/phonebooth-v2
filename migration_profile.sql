-- ============================================================
-- Migration: Progressive Profile (Hồ sơ người dùng)
-- Chạy trong Supabase SQL Editor
-- ============================================================

-- 1. Thêm cột profile vào users
ALTER TABLE users ADD COLUMN IF NOT EXISTS occupation text;
ALTER TABLE users ADD COLUMN IF NOT EXISTS purpose text;

-- Index cho targeting query
CREATE INDEX IF NOT EXISTS idx_users_occupation ON users (occupation) WHERE occupation IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_users_purpose ON users (purpose) WHERE purpose IS NOT NULL;

-- 2. Update login_user() để trả thêm occupation, purpose
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
    'created_at', v_user.created_at
  );
END;
$$;

-- 3. Update register_user() để trả thêm occupation, purpose
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
    'created_at', v_user.created_at
  );
END;
$$;
