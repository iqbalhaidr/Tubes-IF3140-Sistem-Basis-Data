add_library(qo_lib STATIC
    query_optimizer.cpp
)
target_include_directories(qo_lib PUBLIC ../../include)
