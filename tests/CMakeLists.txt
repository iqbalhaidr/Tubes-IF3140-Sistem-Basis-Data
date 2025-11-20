# Aktifkan testing
enable_testing()

# Tambahkan logika untuk CTest/GTest di sini nanti

add_executable(test_storage_manager test_storage_manager.cpp)
target_link_libraries(test_storage_manager sm_lib)
target_include_directories(test_storage_manager PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_executable(test_qp_sm_integration test_qp_sm_integration.cpp)
target_link_libraries(test_qp_sm_integration
    PRIVATE
    qp_lib
    qo_lib
    sm_lib
    ccm_lib
    fr_lib
)
target_include_directories(test_qp_sm_integration PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_executable(test_qp_qo_integration test_qp_qo_integration.cpp)
target_link_libraries(test_qp_qo_integration
    PRIVATE
    qp_lib
    qo_lib
    sm_lib
    ccm_lib
    fr_lib
)
target_include_directories(test_qp_qo_integration PRIVATE ${PROJECT_SOURCE_DIR}/include)