add_library(ccm_lib STATIC
    concurrency_control.cpp
)
target_include_directories(ccm_lib PUBLIC ../../include)
