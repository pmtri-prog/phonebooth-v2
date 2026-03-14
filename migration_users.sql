-- ============================================================
-- Migration: Lazy Registration - Users + Session Linking
-- Chay trong Supabase SQL Editor
-- ============================================================

-- 0. Enable pgcrypto for password hashing
CREATE EXTENSION IF NOT EXISTS pgcrypto;

-- 1. Bang users
CREATE TABLE users (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  phone           text NOT NULL UNIQUE,
  password_hash   text NOT NULL,
  name            text DEFAULT '',
  created_at      timestamptz NOT NULL DEFAULT now(),
  last_login      timestamptz NOT NULL DEFAULT now()
);

-- Index for phone lookup (login)
CREATE INDEX idx_users_phone ON users (phone);

-- RLS
ALTER TABLE users ENABLE ROW LEVEL SECURITY;
CREATE POLICY "Allow all for users" ON users
  FOR ALL USING (true) WITH CHECK (true);

-- Protect password_hash from direct SELECT via anon key
REVOKE SELECT (password_hash) ON users FROM anon;

-- 2. Them user_id vao sessions (nullable, vi ESP32 tao session khong biet user)
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS user_id uuid REFERENCES users(id);
CREATE INDEX idx_sessions_user ON sessions (user_id) WHERE user_id IS NOT NULL;

-- 3. Register function (SECURITY DEFINER - hash server-side, khong expose password_hash)
CREATE OR REPLACE FUNCTION register_user(p_phone text, p_password text)
RETURNS json
LANGUAGE plpgsql
SECURITY DEFINER
AS $$
DECLARE
  v_user users%ROWTYPE;
BEGIN
  -- Check if phone already exists
  SELECT * INTO v_user FROM users WHERE phone = p_phone;
  IF FOUND THEN
    RETURN json_build_object('error', 'PHONE_EXISTS');
  END IF;

  INSERT INTO users (phone, password_hash)
  VALUES (p_phone, crypt(p_password, gen_salt('bf')))
  RETURNING * INTO v_user;

  RETURN json_build_object(
    'id', v_user.id,
    'phone', v_user.phone,
    'name', v_user.name,
    'created_at', v_user.created_at
  );
END;
$$;

-- 4. Login function
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

  -- Update last_login
  UPDATE users SET last_login = now() WHERE id = v_user.id;

  RETURN json_build_object(
    'id', v_user.id,
    'phone', v_user.phone,
    'name', v_user.name,
    'created_at', v_user.created_at
  );
END;
$$;
