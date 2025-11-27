add_subdirectory(query_optimizer)
add_subdirectory(storage_manager)
add_subdirectory(concurrency_control)
add_subdirectory(failure_recovery)
add_subdirectory(query_processor)
add_subdirectory(tui)

# Buat executable utama
add_executable(mdbms_client main.cpp)

# Link executable utama dengan SEMUA library komponen
target_link_libraries(mdbms_client
    PRIVATE
    qp_lib
    qo_lib
    sm_lib
    ccm_lib
    fr_lib
    tui_lib
)


# execute test_query_optimizer
add_executable(test_query_optimizer
    ${CMAKE_SOURCE_DIR}/tests/test_query_optimizer.cpp
)

target_link_libraries(test_query_optimizer 
    PRIVATE
    qo_lib
    sm_lib
)

add_executable(test_query_processor
    ${CMAKE_SOURCE_DIR}/tests/test_query_processor.cpp
)

target_link_libraries(test_query_processor
    PRIVATE
    qp_lib
    qo_lib
    sm_lib
    ccm_lib
    fr_lib
)
