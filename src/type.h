#pragma once

#include <memory>
#include <string>
#include <vector>

enum class TypeKind { Int, Bool, Void, Vector };

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
