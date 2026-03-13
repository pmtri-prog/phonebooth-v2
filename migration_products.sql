-- ============================================================
-- Product Placement Phase 1 - Migration
-- ============================================================

-- Bảng brands: thông tin thương hiệu
CREATE TABLE brands (
  id              uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  name            text NOT NULL,
  logo_url        text DEFAULT '',
  contact_person  text DEFAULT '',
  contact_email   text DEFAULT '',
  contact_phone   text DEFAULT '',
  created_at      timestamptz NOT NULL DEFAULT now()
);

-- Bảng products: sản phẩm của Brand hoặc Epione
CREATE TABLE products (
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
CREATE TABLE booth_products (
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
CREATE TABLE product_impressions (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id          text NOT NULL,
  booth_product_id uuid NOT NULL REFERENCES booth_products(id) ON DELETE CASCADE,
  source           text NOT NULL CHECK (source IN ('post_session', 'post_session_detail', 'check_in', 'check_in_detail', 'qr_scan')),
  created_at       timestamptz NOT NULL DEFAULT now()
);

-- Bảng product_leads: ghi nhận lead khi user nhấn CTA
CREATE TABLE product_leads (
  id               uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id          text NOT NULL,
  booth_product_id uuid NOT NULL REFERENCES booth_products(id) ON DELETE CASCADE,
  cta_type         text NOT NULL CHECK (cta_type IN ('get_voucher', 'interested')),
  source           text NOT NULL CHECK (source IN ('post_session', 'check_in', 'qr_scan')),
  voucher_redeemed boolean NOT NULL DEFAULT false,
  created_at       timestamptz NOT NULL DEFAULT now()
);

-- Indexes
CREATE INDEX idx_booth_products_active ON booth_products (booth_id, status)
  WHERE status = 'active';
CREATE INDEX idx_booth_products_dates ON booth_products (booth_id, start_date, end_date)
  WHERE status = 'active';
CREATE INDEX idx_product_leads_user ON product_leads (user_id, booth_product_id);
CREATE INDEX idx_product_impressions_date ON product_impressions (created_at);

-- RLS: cho phép anon key đọc/ghi (MVP)
ALTER TABLE brands ENABLE ROW LEVEL SECURITY;
ALTER TABLE products ENABLE ROW LEVEL SECURITY;
ALTER TABLE booth_products ENABLE ROW LEVEL SECURITY;
ALTER TABLE product_impressions ENABLE ROW LEVEL SECURITY;
ALTER TABLE product_leads ENABLE ROW LEVEL SECURITY;

CREATE POLICY "Allow all for brands" ON brands
  FOR ALL USING (true) WITH CHECK (true);
CREATE POLICY "Allow all for products" ON products
  FOR ALL USING (true) WITH CHECK (true);
CREATE POLICY "Allow all for booth_products" ON booth_products
  FOR ALL USING (true) WITH CHECK (true);
CREATE POLICY "Allow all for product_impressions" ON product_impressions
  FOR ALL USING (true) WITH CHECK (true);
CREATE POLICY "Allow all for product_leads" ON product_leads
  FOR ALL USING (true) WITH CHECK (true);
