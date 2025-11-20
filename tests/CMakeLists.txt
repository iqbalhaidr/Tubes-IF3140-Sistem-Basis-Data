# Build test_concurrency_control
add_executable(test_concurrency_control test_concurrency_control.cpp)
target_link_libraries(test_concurrency_control PRIVATE ccm_lib)
target_include_directories(test_concurrency_control PRIVATE ../include)

# Aktifkan testing
enable_testing()

# Tambahkan logika untuk CTest/GTest di sini nanti

# Logika untuk melakukan testing test_storage_manager.cpp
add_executable(test_storage_manager test_storage_manager.cpp)
target_link_libraries(test_storage_manager sm_lib)
target_include_directories(test_storage_manager PRIVATE ${PROJECT_SOURCE_DIR}/include)