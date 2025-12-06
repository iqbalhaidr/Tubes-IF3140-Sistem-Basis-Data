-- =====================================================
-- TEST 2: System Crash Recovery (REDO + UNDO)
-- =====================================================
-- Test scenario:
-- 1. Transaction A: COMMIT (perlu REDO)
-- 2. Transaction B: Tidak COMMIT (perlu UNDO)
-- 3. Kill server (simulate crash)
-- 4. Restart server
-- 5. Verify: Transaction A data ada, Transaction B data tidak ada

-- PART 1: Setup data sebelum crash
-- ---------------------------------

-- Transaction A: Akan di-COMMIT (should be REDOne)
BEGIN;
INSERT INTO Supplier VALUES (100, 'Committed Supplier', 'Jakarta');
COMMIT;

-- Transaction B: Tidak di-COMMIT (should be UNDOne)
BEGIN;
INSERT INTO Supplier VALUES (200, 'Uncommitted Supplier', 'Bandung');
-- JANGAN COMMIT! Biarkan transaksi terbuka

-- INSTRUKSI: 
-- 1. Setelah query di atas, JANGAN ketik EXIT atau COMMIT
-- 2. LANGSUNG kill server dengan Ctrl+C di terminal server
-- 3. Restart server dengan: ./build/src/server
-- 4. Jalankan test_recovery_verify.sql untuk verify hasil recovery
