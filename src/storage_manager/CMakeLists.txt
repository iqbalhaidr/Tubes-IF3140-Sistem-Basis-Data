add_library(sm_lib STATIC
    storage_manager.cpp
    hash_index.cpp
)
target_include_directories(sm_lib PUBLIC ../../include)
