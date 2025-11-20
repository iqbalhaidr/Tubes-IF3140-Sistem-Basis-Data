add_library(ccm_lib STATIC
    concurrency_control.cpp
    timestamp_ordering.cpp
    two_phase_locking_protocol.cpp
)
target_include_directories(ccm_lib PUBLIC ../../include)
