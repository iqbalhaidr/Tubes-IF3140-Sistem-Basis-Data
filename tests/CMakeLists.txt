# Aktifkan testing
enable_testing()

# Tambahkan logika untuk CTest/GTest di sini nanti

# Logika untuk melakukan testing test_storage_manager.cpp
add_executable(test_storage_manager test_storage_manager.cpp)
target_link_libraries(test_storage_manager sm_lib)
target_include_directories(test_storage_manager PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Logika untuk melakukan testing test_failure_recovery.cpp
add_executable(test_failure_recovery test_failure_recovery.cpp)
target_link_libraries(test_failure_recovery fr_lib)
target_include_directories(test_failure_recovery PRIVATE ${PROJECT_SOURCE_DIR}/include)