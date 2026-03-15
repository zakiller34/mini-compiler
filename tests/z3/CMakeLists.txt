# Z3 predicate tests
set(CMAKE_CXX_CLANG_TIDY "")

set(Z3_TEST_SOURCES
    test_coloring_z3
    test_liveness_z3
    test_type_checker_z3
)

foreach(test_name IN LISTS Z3_TEST_SOURCES)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE mc_lib GTest::gtest_main z3)
    target_include_directories(${test_name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
