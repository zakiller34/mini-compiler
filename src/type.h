#pragma once

#include <memory>
#include <string>
#include <vector>

namespace mc {

enum class TypeKind { Int, Bool, Void, Vector, Function };

struct Type;
using TypePtr = std::shared_ptr<Type>;

struct Type {
    TypeKind kind;
    std::vector<TypePtr> elem_types; // only for Vector

    bool operator==(const Type &other) const;
    bool operator!=(const Type &other) const { return !(*this == other); }

    /// @brief Human-readable type name
    std::string dump() const;
};

/// @brief Factories
TypePtr int_type();
TypePtr bool_type();
TypePtr void_type();
TypePtr vector_type(std::vector<TypePtr> elems);

/// @brief Check if type is a vector type
bool is_vector_type(const TypePtr &t);

/// @brief Function type factory: (param_types...) -> ret_type
TypePtr fun_type(std::vector<TypePtr> params, TypePtr ret);

/// @brief Check if type is a function type
bool is_fun_type(const TypePtr &t);

} // namespace mc
