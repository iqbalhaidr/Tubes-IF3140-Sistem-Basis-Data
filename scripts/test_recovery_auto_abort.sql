-- =====================================================
-- TEST 4: Auto-commit Transaction Abort
-- =====================================================
-- Test abort untuk auto-commit transaction (tanpa BEGIN explicit)

-- Lihat data awal
select * from Item;

-- Auto-commit transactions yang akan fail/abort
-- Query dengan error akan trigger abort
INSERT INTO Item VALUES (9999, 'Test Item', 100.0, 999, 999);
-- ^ Foreign key constraint violation (SupID=999, CatID=999 tidak exist)

-- Verify data tidak berubah (auto-rollback)
select * from Item WHERE ItemID = 9999;
-- Expected: No rows (insert failed and auto-aborted)

EXIT
