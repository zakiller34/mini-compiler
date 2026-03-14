# src/CMakeLists.txt — Phase 1: L_Var compiler

# Static library with all compiler logic
add_library(mc_lib STATIC
    ast.cpp
    lexer.cpp
    parser.cpp
    ir/c_ir.cpp
    ir/x86_ir.cpp
    interpreter.cpp
    passes/uniquify.cpp
    passes/rco.cpp
    passes/explicate_control.cpp
    passes/select_instructions.cpp
    passes/assign_homes.cpp
    passes/patch_instructions.cpp
    passes/prelude_conclusion.cpp
    passes/emit.cpp
)

target_include_directories(mc_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(mc_lib PUBLIC cxx_std_17)

# Compiler executable
add_executable(mc main.cpp)
target_link_libraries(mc PRIVATE mc_lib)

# Runtime library (C)
add_library(mc_runtime STATIC
    ${CMAKE_SOURCE_DIR}/runtime/runtime.c
)
target_include_directories(mc_runtime PUBLIC
    ${CMAKE_SOURCE_DIR}/runtime
)
