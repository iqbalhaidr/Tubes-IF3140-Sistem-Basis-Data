add_library(tui_lib STATIC
    tui.cpp
)
target_include_directories(tui_lib PUBLIC ../../include)

target_link_libraries(tui_lib
    PRIVATE
    qp_lib
)

