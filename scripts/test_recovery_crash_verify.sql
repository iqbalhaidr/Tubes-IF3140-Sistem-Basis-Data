-- =====================================================
-- TEST 2 VERIFICATION: Setelah System Crash Recovery
-- =====================================================
-- Jalankan script ini SETELAH restart server untuk verify recovery

-- Expected results:
-- ✓ SupID 100 (Committed Supplier) HARUS ADA (REDO)
-- ✓ SupID 200 (Uncommitted Supplier) TIDAK BOLEH ADA (UNDO)

select * from Supplier WHERE SupID IN (100, 200);

-- Jika recovery benar:
-- - Row dengan SupID=100 akan muncul (REDO committed transaction)
-- - Row dengan SupID=200 TIDAK muncul (UNDO uncommitted transaction)

EXIT
