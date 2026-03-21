#include "type.h"

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

std::string Type::dump() const {
    switch (kind) {
    case TypeKind::Int: return "Int";
    case TypeKind::Bool: return "Bool";
    case TypeKind::Void: return "Void";
    case TypeKind::Vector: {
        std::string result = "Vector(";
        // invariant: result has dump of elem_types[0..j)
        bool first = true;
        for (const auto &et : elem_types) {
            if (!first) result += ", ";
            first = false;
            result += et->dump();
        }
        result += ")";
        return result;
    }
    case TypeKind::Function: {
        std::string result = "(";
        // invariant: result has dump of param types [0..j)
        // elem_types = [param0, param1, ..., ret_type]
        for (size_t i = 0; i + 1 < elem_types.size(); ++i) {
            if (i > 0) result += ", ";
            result += elem_types[i]->dump();
        }
        result += ") -> ";
        if (!elem_types.empty()) {
            result += elem_types.back()->dump();
        }
        return result;
    }
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

} // namespace mc
