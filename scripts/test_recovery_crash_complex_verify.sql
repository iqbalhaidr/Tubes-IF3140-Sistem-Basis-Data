-- =====================================================
-- TEST 3 VERIFICATION: Complex Crash Recovery
-- =====================================================

-- Expected results after recovery:
-- ✓ CatID 1001, 1002: HARUS ADA (committed transactions - REDO)
-- ✓ CatID 2001, 2002: TIDAK BOLEH ADA (uncommitted inserts - UNDO)
-- ✓ CatID 10: CatName HARUS 'Modified Category' (committed update - REDO)
-- ✓ CatID 20: HARUS ADA dengan nama asli (uncommitted delete - UNDO restore)

-- Check committed inserts (should exist)
select * from Category WHERE CatID IN (1001, 1002);

-- Check uncommitted inserts (should NOT exist)
select * from Category WHERE CatID IN (2001, 2002);

-- Check committed update (should be modified)
select * from Category WHERE CatID = 10;

-- Check uncommitted delete (should be restored)
select * from Category WHERE CatID = 20;

-- Show all categories to see full picture
select * from Category ORDER BY CatID;

EXIT
