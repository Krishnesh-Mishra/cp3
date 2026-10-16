// =============================================================================
// cp3/types.cpp — the cp3 value-type model (see cp3/types.hpp).
//
// Only the primitive rules are load-bearing in M1; the compound-kind branches
// below are shape-frozen and honest, not complete.
// =============================================================================
#include "cp3/types.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

namespace cp3 {
namespace {

// Append `t` to `out` unless an equal member is already present.
void addUnique(std::vector<Type>& out, const Type& t) {
    for (const Type& existing : out) {
        if (existing == t) {
            return;
        }
    }
    out.push_back(t);
}

// True when `t` is a Union/Nullable container whose members should be spliced
// into a surrounding Union.
bool isJoinContainer(const Type& t) noexcept {
    return t.kind == TypeKind::Union || t.kind == TypeKind::Nullable;
}

// True when a container type admits the `null` / `undefined` values
// (Nullable, a union containing a nullish member, or a bare nullish type).
bool acceptsNullish(const Type& t) noexcept {
    if (t.isNullish() || t.isNullable()) {
        return true;
    }
    if (t.isUnion()) {
        for (const Type& member : t.parts) {
            if (member.isNullish()) {
                return true;
            }
        }
    }
    return false;
}

// Format a finite double for a diagnostic without depending on the runtime's
// ECMAScript formatter (that lives in cp3/runtime.hpp). Diagnostics are
// best-effort; %g keeps it short and stable.
std::string formatDouble(double value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", value);
    return std::string(buf);
}

}  // namespace

const char* typeKindName(TypeKind kind) noexcept {
    switch (kind) {
        case TypeKind::Error: return "error";
        case TypeKind::Unknown: return "unknown";
        case TypeKind::Number: return "number";
        case TypeKind::String: return "string";
        case TypeKind::Boolean: return "boolean";
        case TypeKind::Undefined: return "undefined";
        case TypeKind::Null: return "null";
        case TypeKind::Void: return "void";
        case TypeKind::Array: return "array";
        case TypeKind::Function: return "function";
        case TypeKind::Object: return "object";
        case TypeKind::Class: return "class";
        case TypeKind::Enum: return "enum";
        case TypeKind::Union: return "union";
        case TypeKind::Nullable: return "nullable";
        case TypeKind::TypeParam: return "typeparam";
        case TypeKind::Literal: return "literal";
    }
    return "unknown";
}

// --- primitive constructors --------------------------------------------------

Type Type::error() { return Type(TypeKind::Error); }
Type Type::unknown() { return Type(TypeKind::Unknown); }
Type Type::number() { return Type(TypeKind::Number); }
Type Type::string() { return Type(TypeKind::String); }
Type Type::boolean() { return Type(TypeKind::Boolean); }
Type Type::undefined() { return Type(TypeKind::Undefined); }
Type Type::nullType() { return Type(TypeKind::Null); }
Type Type::voidType() { return Type(TypeKind::Void); }

// --- literal constructors ----------------------------------------------------

Type Type::numberLiteral(double value) {
    Type t(TypeKind::Literal);
    t.literalBase = TypeKind::Number;
    t.numberValue = value;
    return t;
}

Type Type::stringLiteral(std::string value) {
    Type t(TypeKind::Literal);
    t.literalBase = TypeKind::String;
    t.stringValue = std::move(value);
    return t;
}

Type Type::boolLiteral(bool value) {
    Type t(TypeKind::Literal);
    t.literalBase = TypeKind::Boolean;
    t.boolValue = value;
    return t;
}

// --- compound constructors ---------------------------------------------------

Type Type::array(Type element) {
    Type t(TypeKind::Array);
    t.parts.push_back(std::move(element));
    return t;
}

Type Type::function(std::vector<Type> params, Type returnType) {
    Type t(TypeKind::Function);
    t.parts = std::move(params);
    t.parts.push_back(std::move(returnType));
    return t;
}

Type Type::unionOf(std::vector<Type> members) {
    // Flatten and de-duplicate so structurally equal unions compare equal.
    std::vector<Type> flat;
    for (const Type& m : members) {
        if (isJoinContainer(m)) {
            for (const Type& inner : m.parts) {
                addUnique(flat, inner);
            }
        } else {
            addUnique(flat, m);
        }
    }
    if (flat.size() == 1) {
        return flat.front();
    }
    Type t(TypeKind::Union);
    t.parts = std::move(flat);
    return t;
}

Type Type::nullable(Type inner) {
    Type t(TypeKind::Nullable);
    t.parts.push_back(std::move(inner));
    return t;
}

Type Type::named(TypeKind kind, std::string name) {
    Type t(kind);
    t.name = std::move(name);
    return t;
}

// --- predicates / accessors --------------------------------------------------

Type Type::literalBaseType() const {
    if (kind != TypeKind::Literal) {
        return *this;
    }
    switch (literalBase) {
        case TypeKind::Number: return Type::number();
        case TypeKind::String: return Type::string();
        case TypeKind::Boolean: return Type::boolean();
        default: return Type::unknown();
    }
}

bool operator==(const Type& a, const Type& b) noexcept {
    return a.kind == b.kind && a.parts == b.parts && a.name == b.name &&
           a.literalBase == b.literalBase && a.numberValue == b.numberValue &&
           a.stringValue == b.stringValue && a.boolValue == b.boolValue;
}

bool isPrimitive(const Type& t) noexcept {
    switch (t.kind) {
        case TypeKind::Number:
        case TypeKind::String:
        case TypeKind::Boolean:
        case TypeKind::Undefined:
        case TypeKind::Null:
            return true;
        default:
            return false;
    }
}

// --- describe -----------------------------------------------------------------

std::string describe(const Type& t) {
    switch (t.kind) {
        case TypeKind::Array:
            return t.parts.empty() ? "array" : describe(t.parts.front()) + "[]";
        case TypeKind::Function: {
            std::string out = "(";
            if (t.parts.size() >= 2) {
                const std::size_t params = t.parts.size() - 1;
                for (std::size_t i = 0; i < params; ++i) {
                    if (i != 0) {
                        out += ", ";
                    }
                    out += describe(t.parts[i]);
                }
            }
            out += ") => ";
            out += t.parts.empty() ? "unknown" : describe(t.parts.back());
            return out;
        }
        case TypeKind::Union: {
            if (t.parts.empty()) {
                return "never";
            }
            std::string out;
            for (std::size_t i = 0; i < t.parts.size(); ++i) {
                if (i != 0) {
                    out += " | ";
                }
                out += describe(t.parts[i]);
            }
            return out;
        }
        case TypeKind::Nullable: {
            std::string out = t.parts.empty() ? "unknown" : describe(t.parts.front());
            out += " | null | undefined";
            return out;
        }
        case TypeKind::Object:
        case TypeKind::Class:
        case TypeKind::Enum:
        case TypeKind::TypeParam:
            return t.name.empty() ? std::string(typeKindName(t.kind)) : t.name;
        case TypeKind::Literal:
            switch (t.literalBase) {
                case TypeKind::Number: return formatDouble(t.numberValue);
                case TypeKind::String: return "\"" + t.stringValue + "\"";
                case TypeKind::Boolean: return t.boolValue ? "true" : "false";
                default: return "literal";
            }
        default:
            return typeKindName(t.kind);
    }
}

// --- assignability ------------------------------------------------------------

bool isAssignable(const Type& from, const Type& to) {
    // Error / Unknown: permissive so one mistake does not cascade.
    if (from.isError() || to.isError()) {
        return true;
    }
    if (from.isUnknown() || to.isUnknown()) {
        return true;
    }
    if (from == to) {
        return true;
    }

    // A literal is assignable exactly when its base primitive is.
    if (from.isLiteral()) {
        return isAssignable(from.literalBaseType(), to);
    }
    if (to.isLiteral()) {
        // Only the exact literal (handled by operator== above) matches.
        return false;
    }

    // Narrowing out of a union / nullable: every member must fit. This is
    // checked before widening so union-to-union is decided member-wise.
    if (from.isUnion()) {
        for (const Type& member : from.parts) {
            if (!isAssignable(member, to)) {
                return false;
            }
        }
        return true;
    }
    if (from.isNullable()) {
        // null | undefined are only assignable to a container that admits them.
        if (!acceptsNullish(to)) {
            return false;
        }
        for (const Type& member : from.parts) {
            if (!isAssignable(member, to)) {
                return false;
            }
        }
        return true;
    }

    // Widening into a union / nullable container.
    if (to.isUnion()) {
        for (const Type& member : to.parts) {
            if (isAssignable(from, member)) {
                return true;
            }
        }
        return false;
    }
    if (to.isNullable()) {
        if (from.isNullish()) {
            return true;
        }
        for (const Type& member : to.parts) {
            if (isAssignable(from, member)) {
                return true;
            }
        }
        return false;
    }

    // Every remaining kind (primitives with different kinds, structural
    // objects, classes, enums, generics) matches only itself — which the
    // `from == to` check above already covered. Stay honest: no claim of
    // compatibility for deferred kinds.
    return false;
}

// --- unification --------------------------------------------------------------

Type unify(const Type& a, const Type& b) {
    if (a == b) {
        return a;
    }
    if (a.isError()) {
        return b;
    }
    if (b.isError()) {
        return a;
    }
    if (a.isUnknown()) {
        return b;
    }
    if (b.isUnknown()) {
        return a;
    }
    if (a.isVoid() || b.isVoid()) {
        return Type::voidType();
    }

    // A literal expression widens to its base primitive when unified.
    if (a.isLiteral()) {
        return unify(a.literalBaseType(), b);
    }
    if (b.isLiteral()) {
        return unify(a, b.literalBaseType());
    }

    // nullish joins collapse to the Nullable shorthand (T | null | undefined).
    if (a.isNullish() && b.isNullish()) {
        return Type::unionOf({a, b});
    }
    if (a.isNullish()) {
        return Type::nullable(b);
    }
    if (b.isNullish()) {
        return Type::nullable(a);
    }

    std::vector<Type> members;
    auto addMembers = [&members](const Type& t) {
        if (t.isUnion() || t.isNullable()) {
            for (const Type& part : t.parts) {
                addUnique(members, part);
            }
        } else {
            addUnique(members, t);
        }
    };
    addMembers(a);
    addMembers(b);

    if (members.size() == 1) {
        return members.front();
    }
    return Type::unionOf(std::move(members));
}

}  // namespace cp3
