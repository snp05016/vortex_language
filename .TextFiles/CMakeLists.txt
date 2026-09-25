cmake_minimum_required(VERSION 3.15)
project(Vortex LANGUAGES CXX)

enable_testing()

add_library(vortex_compiler
    src/frontend/lexer.cpp
    src/frontend/parser/parser.cpp
    src/frontend/parser/parser_expressions.cpp
    src/frontend/parser/parser_statements.cpp
    src/frontend/parser/parser_declarations.cpp
    src/frontend/parser/parser_types.cpp
    )
target_include_directories(vortex_compiler PUBLIC src)
target_compile_options(vortex_compiler PRIVATE -std=c++26)

add_executable(vortex_debug
    src/main.cpp
)

target_link_libraries(vortex_debug PRIVATE vortex_compiler)
target_compile_options(vortex_debug PRIVATE -std=c++26)

add_executable(vortex_lexer_tests
    tests/lexer_tests.cpp
)

target_link_libraries(vortex_lexer_tests PRIVATE vortex_compiler)
target_compile_options(vortex_lexer_tests PRIVATE -std=c++26)

add_test(NAME lexer_tests COMMAND vortex_lexer_tests)

add_custom_target(format
    COMMAND "${CMAKE_SOURCE_DIR}/scripts/format.sh"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Formatting Vortex C++ sources"
)
