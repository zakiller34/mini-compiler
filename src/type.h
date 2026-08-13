#pragma once

#include <memory>
#include <string>
#include <vector>

namespace mc {

enum class TypeKind { Int, Bool, Void, Vector, Function, Any };

/// @brief Runtime tag codes (Siek 2023, section 9.2). 000 is reserved for
///        untagged tuple pointers so the GC can tell them from tagged values.
constexpr int64_t kTagInt = 0b001;
constexpr int64_t kTagVector = 0b010;
constexpr int64_t kTagFunction = 0b011;
constexpr int64_t kTagBool = 0b100;
constexpr int64_t kTagVoid = 0b101;
constexpr int64_t kTagMask = 0b111;
constexpr int64_t kTagShift = 3;

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

/// @brief The dynamic type Any (Siek 2023, section 9.3)
TypePtr any_type();

/// @brief Check if type is Any
bool is_any_type(const TypePtr &t);

/// @brief Flat types: the only types Inject/Project may mention
/// @ensures result iff t is Int, Bool, Void, (Vector Any...) or (Any... -> Any)
bool is_flat_type(const TypePtr &t);

/// @brief Runtime tag code for a flat type
/// @requires is_flat_type(t)
/// @ensures result in {kTagInt, kTagBool, kTagVector, kTagFunction, kTagVoid}
int64_t tagof(const TypePtr &t);

/// @brief Types whose values may be heap pointers, so they must live on the
///        root stack and be scanned by the GC
/// @ensures result == (is_vector_type(t) || is_any_type(t))
bool is_root_type(const TypePtr &t);

} // namespace mc
