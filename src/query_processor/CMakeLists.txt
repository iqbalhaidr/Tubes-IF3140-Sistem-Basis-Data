# Buat library untuk Query Processor
add_library(qp_lib STATIC
    query_processor.cpp
)

# Pastikan library ini bisa menemukan header publik
target_include_directories(qp_lib PUBLIC ../../include)

# Link dependencies - qp_lib uses symbols from these libraries
target_link_libraries(qp_lib
    PRIVATE
    qo_lib
    sm_lib
    ccm_lib
    fr_lib
)
