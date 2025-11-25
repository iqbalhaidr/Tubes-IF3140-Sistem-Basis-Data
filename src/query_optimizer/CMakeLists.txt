# Query Optimizer Library
add_library(qo_lib STATIC
    query_optimizer.cpp
    parser/sql_parser.cpp
    parser/dml/dml_parser.cpp
    parser/dml/select_parser.cpp
    parser/ddl/ddl_parser.cpp
    parser/utils/parser_utils.cpp
    parser/utils/string_utils.cpp
    tree/plan_tree.cpp
    optimizer/optimizer_rules.cpp
    optimizer/optimizer_helper.cpp
    cost/cost_model.cpp
)

# PUBLIC = buat Query Processor
# PRIVATE = internal punya query_optimizer
target_include_directories(qo_lib
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/parser
        ${CMAKE_CURRENT_SOURCE_DIR}/tree
        ${CMAKE_CURRENT_SOURCE_DIR}/optimizer
        ${CMAKE_CURRENT_SOURCE_DIR}/cost
)
