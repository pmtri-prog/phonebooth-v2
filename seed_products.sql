-- ============================================================
-- Product Placement Phase 1 - Migration + Seed Data
-- Chạy toàn bộ file này trong Supabase SQL Editor
-- ============================================================

-- ======================== MIGRATION ========================

-- Bảng brands: thông tin thương hiệu
CREATE TABLE IF NOT EXISTS brands (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  name            text NOT NULL,
  logo_url        text DEFAULT '',
  contact_person  text DEFAULT '',
  contact_email   text DEFAULT '',
  contact_phone   text DEFAULT '',
  created_at      timestamptz NOT NULL DEFAULT now()
);

-- Bảng products: sản phẩm của Brand hoặc Epione
CREATE TABLE IF NOT EXISTS products (
  id                  uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  brand_id            uuid NOT NULL REFERENCES brands(id) ON DELETE CASCADE,
  name                text NOT NULL,
  description         text DEFAULT '',
  images              text[] DEFAULT '{}',
  video_url           text DEFAULT '',
  price               numeric DEFAULT NULL,
  cta_type            text NOT NULL DEFAULT 'interested' CHECK (cta_type IN ('get_voucher', 'interested')),
  cta_link            text DEFAULT '',
  voucher_code        text DEFAULT '',
  voucher_description text DEFAULT '',
  is_epione           boolean NOT NULL DEFAULT false,
  created_at          timestamptz NOT NULL DEFAULT now()
);

-- Bảng booth_products: gán sản phẩm vào booth
CREATE TABLE IF NOT EXISTS booth_products (
  id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  booth_id    text NOT NULL REFERENCES booths(booth_id),
  product_id  uuid NOT NULL REFERENCES products(id) ON DELETE CASCADE,
  start_date  date NOT NULL DEFAULT CURRENT_DATE,
  end_date    date NOT NULL DEFAULT (CURRENT_DATE + INTERVAL '30 days'),
  priority    int NOT NULL DEFAULT 0,
  qr_code_url text DEFAULT '',
  status      text NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'inactive')),
  created_at  timestamptz NOT NULL DEFAULT now()
);

-- Bảng product_impressions: tracking hiển thị sản phẩm
CREATE TABLE IF NOT EXISTS product_impressions (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id          text NOT NULL,
  booth_product_id uuid NOT NULL REFERENCES booth_products(id) ON DELETE CASCADE,
  source           text NOT NULL CHECK (source IN ('post_session', 'post_session_detail', 'check_in', 'check_in_detail', 'qr_scan')),
  created_at       timestamptz NOT NULL DEFAULT now()
);

-- Bảng product_leads: ghi nhận lead khi user nhấn CTA
CREATE TABLE IF NOT EXISTS product_leads (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id          text NOT NULL,
  booth_product_id uuid NOT NULL REFERENCES booth_products(id) ON DELETE CASCADE,
  cta_type         text NOT NULL CHECK (cta_type IN ('get_voucher', 'interested')),
  source           text NOT NULL CHECK (source IN ('post_session', 'check_in', 'qr_scan')),
  voucher_redeemed boolean NOT NULL DEFAULT false,
  created_at       timestamptz NOT NULL DEFAULT now()
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_booth_products_active ON booth_products (booth_id, status)
  WHERE status = 'active';
CREATE INDEX IF NOT EXISTS idx_booth_products_dates ON booth_products (booth_id, start_date, end_date)
  WHERE status = 'active';
CREATE INDEX IF NOT EXISTS idx_product_leads_user ON product_leads (user_id, booth_product_id);
CREATE INDEX IF NOT EXISTS idx_product_impressions_date ON product_impressions (created_at);

-- RLS: cho phép anon key đọc/ghi (MVP)
ALTER TABLE brands ENABLE ROW LEVEL SECURITY;
ALTER TABLE products ENABLE ROW LEVEL SECURITY;
ALTER TABLE booth_products ENABLE ROW LEVEL SECURITY;
ALTER TABLE product_impressions ENABLE ROW LEVEL SECURITY;
ALTER TABLE product_leads ENABLE ROW LEVEL SECURITY;

DO $$ BEGIN
  IF NOT EXISTS (SELECT 1 FROM pg_policies WHERE tablename = 'brands' AND policyname = 'Allow all for brands') THEN
    CREATE POLICY "Allow all for brands" ON brands FOR ALL USING (true) WITH CHECK (true);
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_policies WHERE tablename = 'products' AND policyname = 'Allow all for products') THEN
    CREATE POLICY "Allow all for products" ON products FOR ALL USING (true) WITH CHECK (true);
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_policies WHERE tablename = 'booth_products' AND policyname = 'Allow all for booth_products') THEN
    CREATE POLICY "Allow all for booth_products" ON booth_products FOR ALL USING (true) WITH CHECK (true);
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_policies WHERE tablename = 'product_impressions' AND policyname = 'Allow all for product_impressions') THEN
    CREATE POLICY "Allow all for product_impressions" ON product_impressions FOR ALL USING (true) WITH CHECK (true);
  END IF;
  IF NOT EXISTS (SELECT 1 FROM pg_policies WHERE tablename = 'product_leads' AND policyname = 'Allow all for product_leads') THEN
    CREATE POLICY "Allow all for product_leads" ON product_leads FOR ALL USING (true) WITH CHECK (true);
  END IF;
END $$;

-- ======================== SEED DATA ========================

-- Brand 1: Highlands Coffee
INSERT INTO brands (id, name, logo_url, contact_person, contact_email, contact_phone)
VALUES (
  'a1111111-1111-1111-1111-111111111111',
  'Highlands Coffee',
  'https://upload.wikimedia.org/wikipedia/vi/thumb/4/4d/Logo_Highlands_Coffee.svg/1200px-Logo_Highlands_Coffee.svg.png',
  'Nguyễn Văn A',
  'partner@highlands.vn',
  '0901234567'
);

-- Brand 2: The Coffee House
INSERT INTO brands (id, name, logo_url, contact_person, contact_email, contact_phone)
VALUES (
  'b2222222-2222-2222-2222-222222222222',
  'The Coffee House',
  'https://upload.wikimedia.org/wikipedia/vi/thumb/1/1b/Logo_The_Coffee_House.svg/1200px-Logo_The_Coffee_House.svg.png',
  'Trần Thị B',
  'partner@thecoffeehouse.com',
  '0912345678'
);

-- Brand 3: Epione (nội bộ)
INSERT INTO brands (id, name, logo_url, contact_person, contact_email, contact_phone)
VALUES (
  'c3333333-3333-3333-3333-333333333333',
  'Epione',
  '',
  'Tri Pham',
  'tri@epione.vn',
  '0900000000'
);

-- Product 1: Highlands - Combo Phin Sữa Đá (voucher)
INSERT INTO products (id, brand_id, name, description, images, price, cta_type, voucher_code, voucher_description, is_epione)
VALUES (
  'd4444444-4444-4444-4444-444444444444',
  'a1111111-1111-1111-1111-111111111111',
  'Combo Phin Sữa Đá + Bánh Mì',
  'Thưởng thức combo Phin Sữa Đá cùng Bánh Mì Thịt Nướng tại Highlands Coffee. Ưu đãi đặc biệt chỉ dành cho khách Epione Phonebooth!',
  ARRAY['https://images.unsplash.com/photo-1509042239860-f550ce710b93?w=400', 'https://images.unsplash.com/photo-1495474472287-4d71bcdd2085?w=400'],
  59000,
  'get_voucher',
  'HIGHLANDS-EPIONE-2026',
  'Giảm 30% Combo Phin Sữa Đá + Bánh Mì',
  false
);

-- Product 2: The Coffee House - Trà Đào Cam Sả (voucher)
INSERT INTO products (id, brand_id, name, description, images, price, cta_type, voucher_code, voucher_description, is_epione)
VALUES (
  'e5555555-5555-5555-5555-555555555555',
  'b2222222-2222-2222-2222-222222222222',
  'Trà Đào Cam Sả',
  'Ly Trà Đào Cam Sả best-seller của The Coffee House. Mát lạnh, thanh khiết - hoàn hảo sau phiên làm việc tại Phonebooth!',
  ARRAY['https://images.unsplash.com/photo-1556679343-c7306c1976bc?w=400', 'https://images.unsplash.com/photo-1544145945-f90425340c7e?w=400'],
  45000,
  'get_voucher',
  'TCH-EPIONE-30',
  'Giảm 25% Trà Đào Cam Sả',
  false
);

-- Product 3: Epione - Gói Premium (interested)
INSERT INTO products (id, brand_id, name, description, images, price, cta_type, cta_link, is_epione)
VALUES (
  'f6666666-6666-6666-6666-666666666666',
  'c3333333-3333-3333-3333-333333333333',
  'Gói Premium Epione',
  'Nâng cấp trải nghiệm Phonebooth với gói Premium: ưu tiên đặt booth, thời gian không giới hạn, và nhiều đặc quyền khác.',
  ARRAY['https://images.unsplash.com/photo-1497366216548-37526070297c?w=400'],
  199000,
  'interested',
  'https://epione.vn/premium',
  true
);

-- Product 4: Highlands - Freeze Trà Xanh (voucher)
INSERT INTO products (id, brand_id, name, description, images, price, cta_type, voucher_code, voucher_description, is_epione)
VALUES (
  'a7777777-7777-7777-7777-777777777777',
  'a1111111-1111-1111-1111-111111111111',
  'Freeze Trà Xanh',
  'Freeze Trà Xanh đá xay mát lạnh - thức uống hoàn hảo để recharge sau phiên làm việc hiệu quả!',
  ARRAY['https://images.unsplash.com/photo-1515823064-d6e0c04616a7?w=400'],
  55000,
  'get_voucher',
  'HL-FREEZE-2026',
  'Mua 1 tặng 1 Freeze Trà Xanh',
  false
);

-- Brand 4: Logitech
INSERT INTO brands (id, name, logo_url, contact_person, contact_email, contact_phone)
VALUES (
  'd4444444-4444-4444-4444-444444444400',
  'Logitech',
  'https://upload.wikimedia.org/wikipedia/commons/thumb/1/17/Logitech_logo.svg/1200px-Logitech_logo.svg.png',
  'Logitech VN',
  'partner@logitech.com',
  '0909876543'
) ON CONFLICT (id) DO NOTHING;

-- Product 5: Logitech MX Master 3S (chuột)
INSERT INTO products (id, brand_id, name, description, images, price, cta_type, voucher_code, voucher_description, is_epione)
VALUES (
  'b8888888-8888-8888-8888-888888888888',
  'd4444444-4444-4444-4444-444444444400',
  'Logitech MX Master 3S',
  'Chuột không dây cao cấp MX Master 3S - 8K DPI, cuộn MagSpeed, sạc USB-C, kết nối 3 thiết bị. Hoàn hảo cho làm việc năng suất tại Phonebooth!',
  ARRAY['https://images.unsplash.com/photo-1527864550417-7fd91fc51a46?w=400', 'https://images.unsplash.com/photo-1615663245857-ac93bb7c39e7?w=400'],
  2490000,
  'get_voucher',
  'LOGI-MX3S-EPIONE',
  'Giảm 20% Logitech MX Master 3S',
  false
);

-- Product 6: Logitech MX Keys S (bàn phím)
INSERT INTO products (id, brand_id, name, description, images, price, cta_type, voucher_code, voucher_description, is_epione)
VALUES (
  'b9999999-9999-9999-9999-999999999999',
  'd4444444-4444-4444-4444-444444444400',
  'Logitech MX Keys S',
  'Bàn phím không dây MX Keys S - phím low-profile, backlit thông minh, Smart Actions, kết nối 3 thiết bị. Đánh máy êm ái, chính xác cho dân văn phòng!',
  ARRAY['https://images.unsplash.com/photo-1587829741301-dc798b83add3?w=400', 'https://images.unsplash.com/photo-1595225476474-87563907a212?w=400'],
  2690000,
  'get_voucher',
  'LOGI-MXKEYS-EPIONE',
  'Giảm 15% Logitech MX Keys S',
  false
);

-- Gán sản phẩm vào BOOTH_001 (3 sản phẩm: 2 Brand + 1 Epione)
INSERT INTO booth_products (booth_id, product_id, start_date, end_date, priority, status)
VALUES
  ('BOOTH_001', 'd4444444-4444-4444-4444-444444444444', CURRENT_DATE, CURRENT_DATE + INTERVAL '30 days', 1, 'active'),
  ('BOOTH_001', 'e5555555-5555-5555-5555-555555555555', CURRENT_DATE, CURRENT_DATE + INTERVAL '30 days', 2, 'active'),
  ('BOOTH_001', 'f6666666-6666-6666-6666-666666666666', CURRENT_DATE, CURRENT_DATE + INTERVAL '30 days', 3, 'active');

-- Gán Logitech vào BOOTH_001
INSERT INTO booth_products (booth_id, product_id, start_date, end_date, priority, status)
VALUES
  ('BOOTH_001', 'b8888888-8888-8888-8888-888888888888', CURRENT_DATE, CURRENT_DATE + INTERVAL '30 days', 0, 'active'),
  ('BOOTH_001', 'b9999999-9999-9999-9999-999999999999', CURRENT_DATE, CURRENT_DATE + INTERVAL '30 days', 0, 'active');

-- Gán sản phẩm vào BOOTH_002 (2 sản phẩm + Logitech)
INSERT INTO booth_products (booth_id, product_id, start_date, end_date, priority, status)
VALUES
  ('BOOTH_002', 'e5555555-5555-5555-5555-555555555555', CURRENT_DATE, CURRENT_DATE + INTERVAL '30 days', 1, 'active'),
  ('BOOTH_002', 'a7777777-7777-7777-7777-777777777777', CURRENT_DATE, CURRENT_DATE + INTERVAL '30 days', 2, 'active'),
  ('BOOTH_002', 'b8888888-8888-8888-8888-888888888888', CURRENT_DATE, CURRENT_DATE + INTERVAL '30 days', 0, 'active');
