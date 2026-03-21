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

/// Function value (just a name reference into defs)
struct FunctionValue {
    std::string name;
    int64_t arity;
    bool operator==(const FunctionValue &o) const {
        return name == o.name && arity == o.arity;
    }
};

using Value = std::variant<int64_t, bool, std::monostate, Tuple, FunctionValue>;

struct TupleData {
    std::vector<Value> elems;
};

/// @brief Interpret program using explicit stack (no recursion)
/// @requires prog.body != nullptr
/// @ensures result == semantics of prog
Value interpret(const Program &prog, std::istream &in);

} // namespace mc
