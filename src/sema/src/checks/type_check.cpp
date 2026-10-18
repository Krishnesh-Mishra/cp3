// =============================================================================
// src/sema/src/checks/type_check.cpp - type model helpers, shapes and type resolution.
//
// Defines the shared helpers over the frozen `Type` (widen / stripNullish /
// pretty / typeKey / ...), the implementation-private shape table, and
// `resolveType` + its per-node-kind helpers. Assignability lives in
// assignability.cpp (STYLE.md §8).
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// Helpers on the frozen Type
// =============================================================================
bool isAmbientType(const Type& t) {
    return t.kind == TypeKind::Object && t.name == kAmbientLabel;
}

Type ambient() { return Type(TypeKind::Object, kAmbientLabel); }

Type elemOf(const Type& arrayLike) {
    if (arrayLike.isUnion() || arrayLike.isNullable()) {
        const Type* best = nullptr;
        for (const Type& m : arrayLike.parts) {
            if (m.isNullish()) continue;
            best = &m;
            break;
        }
        return best ? *best : Type::unknown();
    }
    return arrayLike.parts.empty() ? Type::unknown() : arrayLike.parts.front();
}

Type widen(const Type& t) { return t.isLiteral() ? t.literalBaseType() : t; }

bool isNumberish(const Type& t) {
    return t.isNumber() || (t.isLiteral() && t.literalBase == TypeKind::Number);
}

bool isStringish(const Type& t) {
    return t.isString() || (t.isLiteral() && t.literalBase == TypeKind::String);
}

bool isPermissive(const Type& t) {
    return t.isError() || t.isUnknown() || isAmbientType(t);
}

Type stripNullish(const Type& t) {
    if (t.isNullable()) {
        return t.parts.empty() ? Type::unknown() : t.parts.front();
    }
    if (t.isUnion()) {
        std::vector<Type> keep;
        for (const Type& m : t.parts) {
            if (!m.isNullish()) keep.push_back(m);
        }
        if (keep.empty()) return Type::unknown();
        return Type::unionOf(std::move(keep));
    }
    if (t.isNullish()) return Type::unknown();
    return t;
}

bool isStaticallyNullish(const Type& t) {
    if (t.isNullish()) return true;
    if (t.isUnion() && !t.parts.empty()) {
        for (const Type& m : t.parts) {
            if (!m.isNullish()) return false;
        }
        return true;
    }
    return false;
}

bool hasNullish(const Type& t) {
    if (t.isNullish()) return true;
    if (t.isNullable()) return true;
    if (t.isUnion()) {
        for (const Type& m : t.parts) {
            if (m.isNullish()) return true;
        }
    }
    return false;
}

bool isTupleType(const Type& t) {
    return t.kind == TypeKind::Object && t.name.rfind("$t", 0) == 0;
}

bool isPromiseType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "Promise";
}

bool isEventEmitterType(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "EventEmitter";
}

Type promiseOf(const Type& value) {
    Type p(TypeKind::Class, "Promise");
    p.parts.push_back(value);
    return p;
}

Type promiseValue(const Type& t) {
    if (!isPromiseType(t) || t.parts.empty()) return Type::unknown();
    return t.parts.front();
}

std::string pretty(const Type& t) {
    switch (t.kind) {
        case TypeKind::Error: return "error";
        case TypeKind::Unknown: return "unknown";
        case TypeKind::Number: return "number";
        case TypeKind::String: return "string";
        case TypeKind::Boolean: return "boolean";
        case TypeKind::Undefined: return "undefined";
        case TypeKind::Null: return "null";
        case TypeKind::Void: return "void";
        case TypeKind::Array:
            return t.parts.empty() ? "array" : pretty(t.parts.front()) + "[]";
        case TypeKind::Function: return prettyFunction(t);
        case TypeKind::Union: {
            if (t.parts.empty()) return "never";
            std::string out;
            for (std::size_t i = 0; i < t.parts.size(); ++i) {
                if (i != 0) out += " | ";
                out += pretty(t.parts[i]);
            }
            return out;
        }
        case TypeKind::Nullable:
            return (t.parts.empty() ? std::string("unknown")
                                    : pretty(t.parts.front())) +
                   " | null | undefined";
        case TypeKind::Object:
            if (t.name == kAmbientLabel) return "object";
            if (isTupleType(t)) return "tuple";
            return "object";
        case TypeKind::Class:
            if (isPromiseType(t)) {
                return "Promise<" +
                       (t.parts.empty() ? std::string("unknown")
                                        : pretty(t.parts.front())) +
                       ">";
            }
            return t.name.empty() ? "class" : t.name;
        case TypeKind::Enum: return t.name.empty() ? "enum" : t.name;
        case TypeKind::TypeParam: return t.name.empty() ? "type parameter" : t.name;
        case TypeKind::Literal:
            switch (t.literalBase) {
                case TypeKind::Number: return "number";
                case TypeKind::String: return "string";
                case TypeKind::Boolean: return "boolean";
                default: return "literal";
            }
    }
    return "unknown";
}

std::string prettyFunction(const Type& t) {
    std::string out = "(";
    if (t.parts.size() >= 2) {
        for (std::size_t i = 0; i + 1 < t.parts.size(); ++i) {
            if (i != 0) out += ", ";
            out += pretty(t.parts[i]);
        }
    }
    out += ") => ";
    out += t.parts.empty() ? "unknown" : pretty(t.parts.back());
    return out;
}

std::string typeKey(const Type& t) {
    std::string s = typeKindName(t.kind);
    s += '|';
    s += t.name;
    s += '|';
    if (t.isLiteral()) {
        switch (t.literalBase) {
            case TypeKind::Number: s += std::to_string(t.numberValue); break;
            case TypeKind::String: s += t.stringValue; break;
            case TypeKind::Boolean: s += t.boolValue ? "T" : "F"; break;
            default: break;
        }
    }
    for (const Type& p : t.parts) {
        s += '<';
        s += typeKey(p);
        s += '>';
    }
    return s;
}

bool hasFieldNamed(const std::vector<Field>& fields, const std::string& name) {
    for (const Field& f : fields) {
        if (f.name == name) return true;
    }
    return false;
}

// =============================================================================
// Shapes
// =============================================================================
Shape* Analyzer::shapeOf(const Type& t) {
    if (t.kind != TypeKind::Object) return nullptr;
    const auto it = shapes_.find(t.name);
    return it == shapes_.end() ? nullptr : &it->second;
}

Type Analyzer::makeObjectType(std::vector<Field> fields, bool open, bool tuple,
                              std::vector<Type> tupleElems) {
    if (open) {
        return ambient();
    }
    std::string label;
    if (tuple) {
        label = "$t[";
        for (const Type& e : tupleElems) {
            label += typeKey(e);
            label += ',';
        }
        label += ']';
    } else {
        label = "$o{";
        for (const Field& f : fields) {
            label += f.name;
            label += f.optional ? "?" : "";
            label += ':';
            label += typeKey(f.type);
            label += ',';
        }
        label += '}';
    }
    if (shapes_.find(label) == shapes_.end()) {
        Shape s;
        s.fields = std::move(fields);
        s.tuple = tuple;
        s.tupleElems = std::move(tupleElems);
        shapes_.emplace(label, std::move(s));
    }
    return Type(TypeKind::Object, label);
}

const Field* Analyzer::findField(const Shape* shape,
                                 const std::string& name) const {
    if (!shape) return nullptr;
    for (const Field& f : shape->fields) {
        if (f.name == name) return &f;
    }
    return nullptr;
}

// =============================================================================
// Type resolution
// =============================================================================
Type Analyzer::resolveType(TypeNode& node) {
    switch (node.kind) {
        case NodeKind::NamedType:
            return resolveNamedType(static_cast<NamedType&>(node));
        case NodeKind::UnionType:
            return resolveUnionType(static_cast<UnionType&>(node));
        case NodeKind::ArrayType:
            return resolveArrayType(static_cast<ArrayType&>(node));
        case NodeKind::TupleType:
            return resolveTupleType(static_cast<TupleType&>(node));
        case NodeKind::FunctionType:
            return resolveFunctionType(static_cast<FunctionType&>(node));
        case NodeKind::ObjectType:
            return resolveObjectType(static_cast<ObjectType&>(node));
        case NodeKind::PrefixedType:
            return resolvePrefixedType(static_cast<PrefixedType&>(node));
        case NodeKind::LiteralType:
            return resolveLiteralType(static_cast<LiteralType&>(node));
        case NodeKind::ParenthesizedType: {
            auto& p = static_cast<ParenthesizedType&>(node);
            return p.inner ? resolveType(*p.inner) : Type::unknown();
        }
        default:
            return Type::unknown();
    }
}

Type Analyzer::resolveNamedType(NamedType& nt) {
    // Type arguments are resolved eagerly (both for their own diagnostics and
    // for the generic types M1 does model, `Promise<T>`).
    Type firstArg = Type::unknown();
    bool hasArg = false;
    for (auto& ta : nt.typeArgs) {
        if (!ta) continue;
        Type resolved = resolveType(*ta);
        if (!hasArg) {
            firstArg = resolved;
            hasArg = true;
        }
    }
    if (nt.name == "any") {
        checkBannedAny(nt.span);
        return ambient();
    }
    if (nt.name == "number") return Type::number();
    if (nt.name == "string") return Type::string();
    if (nt.name == "boolean") return Type::boolean();
    if (nt.name == "undefined") return Type::undefined();
    if (nt.name == "null") return Type::nullType();
    if (nt.name == "void") return Type::voidType();
    if (nt.name == "unknown") return Type::unknown();
    if (nt.name == "never") return Type::unknown();
    if (nt.name == "object") return ambient();
    // `Promise<T>` is the one built-in generic (spec §14): a nominal class type
    // carrying its value type. A bare `Promise` is `Promise<unknown>`.
    if (nt.name == "Promise") {
        return promiseOf(hasArg ? firstArg : Type::unknown());
    }
    // The nominal `Buffer` of std/buffer (docs/standard-library/04 §buffer):
    // the annotation resolves only while the `import { Buffer } from
    // "std/buffer"` binding is in scope, so the name never leaks into a
    // program that does not import the module.
    if (isByteBufferTypeName(nt.name)) {
        Binding b;
        if (lookup(nt.name, b) && b.isAmbient) return byteBufferType();
    }
    const auto it = typeNames_.find(nt.name);
    if (it != typeNames_.end()) return it->second;
    report(diagcode::kUndefinedType, nt.span,
           "cannot find type name '" + nt.name + "'");
    return Type::unknown();
}

Type Analyzer::resolveUnionType(UnionType& ut) {
    std::vector<Type> members;
    for (auto& t : ut.types) {
        if (t) members.push_back(resolveType(*t));
    }
    return Type::unionOf(std::move(members));
}

Type Analyzer::resolveArrayType(ArrayType& at) {
    Type e = at.element ? resolveType(*at.element) : Type::unknown();
    return Type::array(e);
}

Type Analyzer::resolveTupleType(TupleType& tt) {
    std::vector<Type> elems;
    for (auto& t : tt.elements) {
        if (t) elems.push_back(resolveType(*t));
    }
    return makeObjectType({}, false, true, std::move(elems));
}

Type Analyzer::resolveFunctionType(FunctionType& ft) {
    std::vector<Type> params;
    for (auto& p : ft.params) {
        if (!p) continue;
        resolveParamSig(*p, params);
    }
    Type ret = ft.returnType ? resolveType(*ft.returnType) : Type::unknown();
    return Type::function(std::move(params), ret);
}

Type Analyzer::resolveObjectType(ObjectType& ot) {
    std::vector<Field> fields;
    bool open = false;
    for (auto& m : ot.members) {
        if (!m) continue;
        if (m->kind == NodeKind::PropertySignature) {
            addPropertySignature(fields, open,
                                 static_cast<PropertySignature&>(*m));
        } else if (m->kind == NodeKind::MethodSignature) {
            addMethodSignature(fields, open, static_cast<MethodSignature&>(*m));
        }
    }
    return makeObjectType(std::move(fields), open, false, {});
}

Type Analyzer::resolvePrefixedType(PrefixedType& pt) {
    if (pt.name.find('*') != std::string::npos ||
        pt.name.find('&') != std::string::npos) {
        report(diagcode::kUnknownTypeArgument, pt.span,
               "raw pointer/reference types are not permitted here");
        return Type::unknown();
    }
    // Raw foreign value types are treated permissively in M1.
    return Type::unknown();
}

Type Analyzer::resolveLiteralType(LiteralType& lt) {
    switch (lt.literalKind) {
        case LiteralTypeKind::String:
            return Type::stringLiteral(lt.stringValue);
        case LiteralTypeKind::Number:
            return Type::numberLiteral(lt.numberValue);
        case LiteralTypeKind::Bool:
            return Type::boolLiteral(lt.boolValue);
    }
    return Type::unknown();
}

void Analyzer::addPropertySignature(std::vector<Field>& fields, bool& open,
                                    PropertySignature& ps) {
    if (ps.computed) {
        report(diagcode::kIndexSignatureBanned, ps.span,
               "index-signature object types are not supported; use Map<K, V> "
               "instead");
        open = true;
        return;
    }
    std::string name = keyName(ps.key.get());
    if (name.empty()) {
        open = true;
        return;
    }
    Type ft = ps.type ? resolveType(*ps.type) : Type::unknown();
    fields.push_back(Field{name, ft, ps.optional});
}

void Analyzer::addMethodSignature(std::vector<Field>& fields, bool& open,
                                  MethodSignature& ms) {
    std::string name = keyName(ms.key.get());
    if (name.empty()) {
        open = true;
        return;
    }
    std::vector<Type> params;
    for (auto& p : ms.params) {
        if (!p) continue;
        resolveParamSig(*p, params);
    }
    Type ret = ms.returnType ? resolveType(*ms.returnType) : Type::unknown();
    fields.push_back(Field{name, Type::function(std::move(params), ret),
                           ms.optional});
}

std::string Analyzer::keyName(Expr* key) const {
    if (!key) return {};
    if (key->kind == NodeKind::Identifier) {
        return static_cast<Identifier&>(*key).name;
    }
    if (key->kind == NodeKind::StringLiteral) {
        return static_cast<StringLiteral&>(*key).value;
    }
    if (key->kind == NodeKind::NumberLiteral) {
        return static_cast<NumberLiteral&>(*key).raw;
    }
    return {};
}

void Analyzer::resolveParamSig(Parameter& p, std::vector<Type>& out) {
    if (p.type) {
        out.push_back(resolveType(*p.type));
    } else {
        out.push_back(Type::unknown());
    }
}

Type Analyzer::nullableIf(bool cond, const Type& t) {
    if (!cond) return t;
    if (hasNullish(t)) return t;
    return unify(t, Type::undefined());
}

Type Analyzer::withoutMember(const Type& t, const Type& remove) {
    if (!t.isUnion() && !t.isNullable()) return t;
    std::vector<Type> keep;
    for (const Type& m : t.parts) {
        if (m == remove) continue;
        keep.push_back(m);
    }
    if (keep.empty()) return Type::unknown();
    return Type::unionOf(std::move(keep));
}

}  // namespace cp3
