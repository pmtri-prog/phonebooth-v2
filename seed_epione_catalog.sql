-- ============================================================
-- Seed: Epione catalog products (Discover tab only)
-- These products are NOT assigned to booth_products,
-- so they won't appear in booth ads or post-session discovery.
-- Run in Supabase SQL Editor
-- ============================================================

-- Ensure Epione brand exists
INSERT INTO brands (id, name, logo_url, contact_person, contact_email, contact_phone)
VALUES (
  'c3333333-3333-3333-3333-333333333333',
  'Epione',
  '',
  'Tri Pham',
  'tri@epione.vn',
  '0977222395'
) ON CONFLICT (id) DO NOTHING;

-- Epione Focus Desk
INSERT INTO products (id, brand_id, name, description, images, price, cta_type, cta_link, is_epione)
VALUES (
  'e1000001-0001-0001-0001-000000000001',
  'c3333333-3333-3333-3333-333333333333',
  'Epione Focus Desk',
  'Bàn làm việc thông minh Epione - mặt bàn rộng 120x60cm, chân điều chỉnh độ cao, tích hợp ổ cắm USB-C và wireless charging. Thiết kế tối giản, phù hợp mọi không gian văn phòng.',
  ARRAY['https://images.unsplash.com/photo-1518455027359-f3f8164ba6bd?w=400', 'https://images.unsplash.com/photo-1593642632559-0c6d3fc62b89?w=400'],
  4990000,
  'interested',
  'https://epione.vn/focus-desk',
  true
);

-- Epione ErgoChair
INSERT INTO products (id, brand_id, name, description, images, price, cta_type, cta_link, is_epione)
VALUES (
  'e1000001-0001-0001-0001-000000000002',
  'c3333333-3333-3333-3333-333333333333',
  'Epione ErgoChair',
  'Ghế công thái học Epione - lưng mesh thoáng khí, tựa đầu điều chỉnh 4D, đệm ngồi memory foam, tay vịn 3D. Ngồi cả ngày không mỏi lưng.',
  ARRAY['https://images.unsplash.com/photo-1580480055273-228ff5388ef8?w=400', 'https://images.unsplash.com/photo-1589364256195-08b23f577e2c?w=400'],
  6990000,
  'interested',
  'https://epione.vn/ergochair',
  true
);

-- Epione Phonebooth (for office rental)
INSERT INTO products (id, brand_id, name, description, images, price, cta_type, cta_link, is_epione)
VALUES (
  'e1000001-0001-0001-0001-000000000003',
  'c3333333-3333-3333-3333-333333333333',
  'Epione Phonebooth',
  'Booth cách âm Epione cho văn phòng - cách âm 35dB, smart lock quản lý qua app, hệ thống thông gió, đèn LED tự động. Lắp đặt trong 1 ngày, thuê theo tháng linh hoạt.',
  ARRAY['https://images.unsplash.com/photo-1497366216548-37526070297c?w=400', 'https://images.unsplash.com/photo-1497215842964-222b430dc094?w=400'],
  NULL,
  'interested',
  '',
  true
);

-- Epione Monitor Arm
INSERT INTO products (id, brand_id, name, description, images, price, cta_type, cta_link, is_epione)
VALUES (
  'e1000001-0001-0001-0001-000000000004',
  'c3333333-3333-3333-3333-333333333333',
  'Epione Monitor Arm',
  'Tay treo màn hình Epione - xoay 360 độ, nâng hạ dễ dàng, hỗ trợ màn 17-32 inch, kẹp bàn chắc chắn. Giải phóng mặt bàn, tối ưu góc nhìn.',
  ARRAY['https://images.unsplash.com/photo-1593642632559-0c6d3fc62b89?w=400'],
  1490000,
  'interested',
  'https://epione.vn/monitor-arm',
  true
);

-- Epione Desk Lamp
INSERT INTO products (id, brand_id, name, description, images, price, cta_type, cta_link, is_epione)
VALUES (
  'e1000001-0001-0001-0001-000000000005',
  'c3333333-3333-3333-3333-333333333333',
  'Epione Desk Lamp',
  'Đèn bàn LED Epione - ánh sáng 3 chế độ (warm/neutral/cool), điều chỉnh độ sáng, cổng sạc USB, chống chói mắt. Bảo vệ mắt khi làm việc lâu.',
  ARRAY['https://images.unsplash.com/photo-1507473885765-e6ed057ab6fe?w=400'],
  890000,
  'interested',
  'https://epione.vn/desk-lamp',
  true
);
