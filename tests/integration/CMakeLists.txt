# Disable clang-tidy for test files
set(CMAKE_CXX_CLANG_TIDY "")

add_executable(test_pipeline test_pipeline.cpp)
target_link_libraries(test_pipeline PRIVATE mc_lib GTest::gtest_main)
target_include_directories(test_pipeline PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME test_pipeline COMMAND test_pipeline)
