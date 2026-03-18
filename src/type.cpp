#include "type.h"

#include <string>

bool Type::operator==(const Type &other) const {
    if (kind != other.kind) return false;
    if (kind != TypeKind::Vector) return true;
    if (elem_types.size() != other.elem_types.size()) return false;
    // invariant: elem_types[0..i) match
    for (size_t i = 0; i < elem_types.size(); ++i) {
        if (*elem_types[i] != *other.elem_types[i]) return false;
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
        // invariant: result has dump of elem_types[0..i)
        for (size_t i = 0; i < elem_types.size(); ++i) {
            if (i > 0) result += ", ";
            result += elem_types[i]->dump();
        }
        result += ")";
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
