#pragma once

#include "ast.h"

#include <istream>
#include <memory>
#include <variant>
#include <vector>

namespace mc {

/// Heap-allocated tuple (elements are Values — forward ref via shared_ptr)
struct TupleData;
using Tuple = std::shared_ptr<TupleData>;

using Value = std::variant<int64_t, bool, std::monostate, Tuple>;

struct TupleData {
    std::vector<Value> elems;
};

/// @brief Interpret program using explicit stack (no recursion)
/// @requires prog.body != nullptr
/// @ensures result == semantics of prog
Value interpret(const Program &prog, std::istream &in);

} // namespace mc
