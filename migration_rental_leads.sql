-- Migration: Create rental_leads table for B2B booth rental inquiries
-- Run in Supabase SQL Editor

CREATE TABLE rental_leads (
  id UUID DEFAULT gen_random_uuid() PRIMARY KEY,
  name TEXT,
  phone TEXT NOT NULL,
  company TEXT,
  source TEXT DEFAULT 'app',  -- 'completion', 'profile', 'landing'
  user_id UUID,
  device_id TEXT,
  session_count INT,
  created_at TIMESTAMPTZ DEFAULT NOW()
);

-- RLS: allow insert from anon
ALTER TABLE rental_leads ENABLE ROW LEVEL SECURITY;
CREATE POLICY "Allow insert rental leads" ON rental_leads FOR INSERT WITH CHECK (true);
CREATE POLICY "Allow admin read rental leads" ON rental_leads FOR SELECT USING (true);
