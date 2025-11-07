add_library(fr_lib STATIC
    failure_recovery.cpp
)
target_include_directories(fr_lib PUBLIC ../../include)
