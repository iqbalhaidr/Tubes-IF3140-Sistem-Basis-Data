# Tambahkan semua 5 subdirektori komponen
add_subdirectory(query_processor)
add_subdirectory(query_optimizer)
add_subdirectory(storage_manager)
add_subdirectory(concurrency_control)
add_subdirectory(failure_recovery)

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
)
