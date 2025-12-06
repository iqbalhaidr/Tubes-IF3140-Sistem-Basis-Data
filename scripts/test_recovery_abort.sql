-- =====================================================
-- TEST 1: Transaction Abort (UNDO Recovery)
-- =====================================================
-- Test scenario: 
-- 1. Lihat data awal
-- 2. BEGIN transaction
-- 3. Lakukan INSERT/UPDATE/DELETE
-- 4. ABORT transaction
-- 5. Verify data kembali ke state awal (UNDO berhasil)

-- Step 1: Lihat data awal Supplier
select * from Supplier;

-- Step 2: Start explicit transaction
BEGIN;

-- Step 3: Modifikasi data (akan di-UNDO)
INSERT INTO Supplier VALUES (999, 'Test Supplier', 'Test City');
UPDATE Supplier SET City = 'MODIFIED' WHERE SupID = 1;
DELETE FROM Supplier WHERE SupID = 2;

-- Step 4: Lihat data setelah modifikasi (dalam transaction)
select * from Supplier;

-- Step 5: ABORT transaction (trigger UNDO)
ABORT;

-- Step 6: Verify data kembali seperti semula
select * from Supplier;

EXIT
