-- =====================================================
-- TEST 3: Multiple Transactions Crash Recovery
-- =====================================================
-- Scenario lebih kompleks dengan multiple transactions

-- T1: COMMIT (should be REDOne)
BEGIN;
INSERT INTO Category VALUES (1001, 'Committed Cat 1');
COMMIT;

-- T2: COMMIT (should be REDOne) 
BEGIN;
INSERT INTO Category VALUES (1002, 'Committed Cat 2');
COMMIT;

-- T3: Tidak COMMIT (should be UNDOne)
BEGIN;
INSERT INTO Category VALUES (2001, 'Uncommitted Cat 1');

-- T4: Tidak COMMIT (should be UNDOne)
BEGIN;
INSERT INTO Category VALUES (2002, 'Uncommitted Cat 2');

-- T5: COMMIT tapi UPDATE (should be REDOne)
BEGIN;
UPDATE Category SET CatName = 'Modified Category' WHERE CatID = 10;
COMMIT;

-- T6: Tidak COMMIT dengan DELETE (should be UNDOne - restore deleted row)
BEGIN;
DELETE FROM Category WHERE CatID = 20;

-- INSTRUKSI:
-- 1. JANGAN EXIT atau COMMIT
-- 2. Kill server (Ctrl+C)
-- 3. Restart server
-- 4. Jalankan test_recovery_complex_verify.sql
