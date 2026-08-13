#include "type.h"

#include <cassert>
#include <string>

namespace mc {

bool Type::operator==(const Type &other) const {
    if (kind != other.kind) return false;
    if (kind != TypeKind::Vector && kind != TypeKind::Function) return true;
    if (elem_types.size() != other.elem_types.size()) return false;
    // invariant: checked[0..j) all match
    auto it = other.elem_types.begin();
    for (const auto &et : elem_types) {
        if (*et != **it++) return false;
    }
    return true;
}

namespace {

/// @brief Render "Vector(T1, T2, ...)"
std::string dump_vector(const std::vector<TypePtr> &elems) {
    std::string result = "Vector(";
    // invariant: result has dump of elems[0..j)
    bool first = true;
    for (const auto &et : elems) {
        if (!first) result += ", ";
        first = false;
        result += et->dump();
    }
    result += ")";
    return result;
}

/// @brief Render "(T1, T2) -> R" from elem_types = [params..., ret]
std::string dump_function(const std::vector<TypePtr> &elems) {
    std::string result = "(";
    // invariant: result has dump of param types [0..i)
    for (size_t i = 0; i + 1 < elems.size(); ++i) {
        if (i > 0) result += ", ";
        result += elems[i]->dump();
    }
    result += ") -> ";
    if (!elems.empty()) result += elems.back()->dump();
    return result;
}

} // namespace

std::string Type::dump() const {
    switch (kind) {
    case TypeKind::Int: return "Int";
    case TypeKind::Bool: return "Bool";
    case TypeKind::Void: return "Void";
    case TypeKind::Any: return "Any";
    case TypeKind::Vector: return dump_vector(elem_types);
    case TypeKind::Function: return dump_function(elem_types);
    }
    return "?";
}

TypePtr int_type() {
    static auto t = std::make_shared<Type>(Type{TypeKind::Int, {}});
    return t;
}

TypePtr bool_type() {
    static auto t = std::make_shared<Type>(Type{TypeKind::Bool, {}});
    return t;
}

TypePtr void_type() {
    static auto t = std::make_shared<Type>(Type{TypeKind::Void, {}});
    return t;
}

TypePtr vector_type(std::vector<TypePtr> elems) {
    return std::make_shared<Type>(Type{TypeKind::Vector, std::move(elems)});
}

bool is_vector_type(const TypePtr &t) {
    return t && t->kind == TypeKind::Vector;
}

TypePtr fun_type(std::vector<TypePtr> params, TypePtr ret) {
    // Store as elem_types = [param0, ..., paramN, ret_type]
    params.push_back(std::move(ret));
    return std::make_shared<Type>(Type{TypeKind::Function, std::move(params)});
}

bool is_fun_type(const TypePtr &t) {
    return t && t->kind == TypeKind::Function;
}

TypePtr any_type() {
    static auto t = std::make_shared<Type>(Type{TypeKind::Any, {}});
    return t;
}

bool is_any_type(const TypePtr &t) {
    return t && t->kind == TypeKind::Any;
}

/// @brief Flat type check
bool is_flat_type(const TypePtr &t) {
    if (!t) return false;
    switch (t->kind) {
    case TypeKind::Int:
    case TypeKind::Bool:
    case TypeKind::Void:
        return true;
    case TypeKind::Vector:
    case TypeKind::Function:
        // invariant: elem_types[0..j) are all Any
        for (const auto &et : t->elem_types) {
            if (!is_any_type(et)) return false;
        }
        return true;
    case TypeKind::Any:
        return false;
    }
    return false;
}

/// @brief Tag code for a flat type
int64_t tagof(const TypePtr &t) {
    assert(is_flat_type(t));
    switch (t->kind) {
    case TypeKind::Int: return kTagInt;
    case TypeKind::Bool: return kTagBool;
    case TypeKind::Void: return kTagVoid;
    case TypeKind::Vector: return kTagVector;
    case TypeKind::Function: return kTagFunction;
    case TypeKind::Any: break;
    }
    assert(!"tagof: not a flat type");
    return kTagInt;
}

bool is_root_type(const TypePtr &t) {
    return is_vector_type(t) || is_any_type(t);
}

} // namespace mc
