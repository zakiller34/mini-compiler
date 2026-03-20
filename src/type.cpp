#include "type.h"

#include <string>

namespace mc {

bool Type::operator==(const Type &other) const {
    if (kind != other.kind) return false;
    if (kind != TypeKind::Vector) return true;
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

} // namespace mc
