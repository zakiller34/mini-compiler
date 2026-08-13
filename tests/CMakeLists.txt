add_subdirectory(unit)

# Z3 predicate tests — only when libz3 is actually present (see root CMakeLists)
if(Z3_LIB)
    add_subdirectory(z3)
endif()

# Integration tests
add_subdirectory(integration)
