-- =====================================================
-- TEST 2 VERIFICATION: Setelah System Crash Recovery
-- =====================================================
-- Workaround untuk bug WHERE IN - gunakan query sederhana

-- Test 1: Check SupID=100 (should exist - REDO committed transaction)
SELECT * FROM Supplier WHERE SupID = 100;

-- Test 2: Check SupID=200 (should NOT exist - UNDO uncommitted transaction)  
SELECT * FROM Supplier WHERE SupID = 200;

-- Expected results:
-- ✓ First query returns 1 row: (100, 'Committed Supplier', 'Jakarta')
-- ✓ Second query returns 0 rows

EXIT
