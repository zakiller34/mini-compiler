# Disable clang-tidy for test files
set(CMAKE_CXX_CLANG_TIDY "")

set(TEST_SOURCES
    test_parser
    test_interpreter
    test_uniquify
    test_rco
    test_explicate
    test_select_instructions
    test_liveness
    test_interference
    test_coloring
    test_assign_homes
    test_patch
    test_type_checker
    test_shrink
    test_uncover_get
    test_reveal_functions
    test_limit_functions
    test_free_vars
    test_convert_assignments
    test_closure_conversion
    test_cast_insert
    test_reveal_casts
)

foreach(test_name IN LISTS TEST_SOURCES)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE mc_lib GTest::gtest_main)
    target_include_directories(${test_name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
