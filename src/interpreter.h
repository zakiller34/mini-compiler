#pragma once

#include "ast.h"

#include <istream>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace mc {

/// Heap-allocated tuple (elements are Values — forward ref via shared_ptr)
struct TupleData;
using Tuple = std::shared_ptr<TupleData>;

/// Lexical closure (captures env snapshot — forward ref via shared_ptr)
struct ClosureData;
using ClosureRef = std::shared_ptr<ClosureData>;

/// Function value (just a name reference into defs)
struct FunctionValue {
    std::string name;
    int64_t arity;
    bool operator==(const FunctionValue &o) const {
        return name == o.name && arity == o.arity;
    }
};

using Value = std::variant<int64_t, bool, std::monostate, Tuple, FunctionValue,
                           ClosureRef>;

struct TupleData {
    std::vector<Value> elems;
};

/// Closure: params + body + captured environment snapshot (copying closures).
/// Equality is pointer identity via the ClosureRef shared_ptr.
struct ClosureData {
    std::vector<std::string> params;
    const Expr *body;
    std::map<std::string, Value> captured;
    int64_t arity;
};

/// @brief Interpret program using explicit stack (no recursion)
/// @requires prog.body != nullptr
/// @ensures result == semantics of prog
Value interpret(const Program &prog, std::istream &in);

} // namespace mc
