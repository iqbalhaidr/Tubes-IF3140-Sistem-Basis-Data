# Buat library untuk Query Processor
add_library(qp_lib STATIC
    query_processor.cpp
)

# Pastikan library ini bisa menemukan header publik
target_include_directories(qp_lib PUBLIC ../../include)
