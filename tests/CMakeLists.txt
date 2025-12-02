# Aktifkan testing
enable_testing()

# Tambahkan logika untuk CTest/GTest di sini nanti

add_executable(test_storage_manager test_storage_manager.cpp)
target_link_libraries(test_storage_manager sm_lib)
target_include_directories(test_storage_manager PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_executable(test_concurrency_control test_concurrency_control.cpp)
target_link_libraries(test_concurrency_control ccm_lib)
target_include_directories(test_concurrency_control PRIVATE ${PROJECT_SOURCE_DIR}/include)


add_executable(test_integration test_integration.cpp)
target_link_libraries(test_integration
    PRIVATE
    qp_lib
    qo_lib
    sm_lib
    ccm_lib
    fr_lib
)
target_include_directories(test_integration PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_executable(test_failure_recovery test_failure_recovery.cpp)
target_link_libraries(test_failure_recovery
    PRIVATE
    fr_lib
    sm_lib
)
target_include_directories(test_failure_recovery PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_executable(test_all_components test_all_components.cpp)

target_link_libraries(test_all_components
    PRIVATE
    qp_lib        # Query Processor
    qo_lib        # Query Optimizer
    sm_lib        # Storage Manager
    ccm_lib       # Concurrency Control Manager
    fr_lib        # Failure Recovery
)

target_include_directories(test_all_components PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_test(NAME test_all_components COMMAND test_all_components)

