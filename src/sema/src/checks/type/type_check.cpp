// =============================================================================
// src/sema/src/checks/type_check.cpp - type model helpers, shapes and type resolution.
//
// Defines the shared helpers over the frozen `Type` (widen / stripNullish /
// pretty / typeKey / ...), the implementation-private shape table, and
// `resolveType` + its per-node-kind helpers. Assignability lives in
// assignability.cpp (STYLE.md §8).
// =============================================================================
#include "detail/analyzer.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cp3/core/utility_types.hpp"
#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/native_types.hpp"

namespace cp3 {

// =============================================================================
// Helpers on the frozen Type
// =============================================================================
bool isAmbientType(const Type& t) {
    return t.kind == TypeKind::Object && t.name == kAmbientLabel;
}

// Can a rest object pattern read this source? A structural object shape is
// the case the Omit is defined for; a class instance is an object too, but
// its members are nominal, so the rest of one is not a shape. A nullish
// option is peeled: the rest reads the object it may hold.
bool isRestSource(const Type& t) {
    const Type peeled = stripNullish(t);
    if (peeled.isUnion()) return false;
    return peeled.kind == TypeKind::Object || peeled.kind == TypeKind::Class;
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

Type defaultLiteralType(TypeSystem ts, const Type& t) {
    if (ts == TypeSystem::TypeScript || !t.isLiteral() ||
        t.literalBase != TypeKind::Number) {
        return widen(t);
    }
    // Exact-spelling gate: only plain integer spellings take a native default
    // (decimal, 0x/0b/0o and `_` separators, optional sign). Fractional and
    // exponent spellings (`2.0`, `1e3`) keep cp3 `number` - Rust's float
    // default is f64, which is `number` here.
    bool negative = false;
    unsigned long long magnitude = 0;
    if (t.numberRaw.empty() ||
        !parseExactIntegerLiteral(t.numberRaw, negative, magnitude)) {
        return Type::number();
    }
    (void)negative;
    (void)magnitude;
    // Exact 64-bit bounds use the source spelling: `18446744073709551615`
    // defaults to `u64`, `-9223372036854775808` to `i64`.
    if (nativeCanRepresentLiteral(Type::native("i32"), t)) {
        return Type::native("i32");
    }
    if (nativeCanRepresentLiteral(Type::native("i64"), t)) {
        return Type::native("i64");
    }
    if (nativeCanRepresentLiteral(Type::native("u64"), t)) {
        return Type::native("u64");
    }
    return Type::number();
}

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
        case TypeKind::Native:
            return t.name.empty() ? "native" : t.name;
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

// =============================================================================
// Mixed-arithmetic suggestion helper (CP3411): names the source text of the
// offending operand and the conversion that restores a numeric expression.
// =============================================================================
static bool isArithmeticNumeric(const Type& t) {
    if (isNumberish(t)) return true;
    if (isNativeType(t)) return true;
    if (t.isLiteral() && t.literalBase == TypeKind::Number) return true;
    if (t.isUnion()) {
        for (const Type& m : t.parts) {
            if (m.isNullish()) continue;
            if (!isArithmeticNumeric(m)) return false;
        }
        return true;
    }
    return false;
}

std::string Analyzer::sourceSlice(const Expr* e) const {
    if (!e || !e->span.file) return {};
    return std::string(e->span.file->slice(e->span));
}

std::vector<std::string> Analyzer::mixedMathHints(const BinaryExpr* be,
                                                  const Type& l, const Type& r) const {
    std::vector<std::string> out;
    const bool lN = isArithmeticNumeric(l);
    const bool rN = isArithmeticNumeric(r);
    const bool lS = isStringish(l);
    const bool rS = isStringish(r);
    const std::string lText = be && be->left ? sourceSlice(be->left.get()) : std::string{};
    const std::string rText = be && be->right ? sourceSlice(be->right.get()) : std::string{};
    const char* target = "number";
    const Expr* offender = nullptr;

    if (lN && !rN && !rS) {
        offender = be && be->right ? be->right.get() : nullptr;
        const Type& other = l;
        if (isNativeType(other)) target = other.name.c_str();
    } else if (rN && !lN && !lS) {
        offender = be && be->left ? be->left.get() : nullptr;
        const Type& other = r;
        if (isNativeType(other)) target = other.name.c_str();
    }
    if (offender != nullptr) {
        const std::string text = sourceSlice(offender);
        if (!text.empty()) {
            out.push_back("convert '" + text + "' with parseTo(" + text + ", " +
                          target + ")");
        } else {
            out.push_back(std::string("convert the other operand with parseTo(x, ") +
                          target + ")");
        }
        return out;
    }
    // No plainly-numeric anchor (string vs number, boolean vs string, ...): give
    // the two source texts so the author can decide which side to change.
    out.push_back("both operands differ in kind ('" +
                  (lText.empty() ? std::string("<left>") : lText) + "' vs '" +
                  (rText.empty() ? std::string("<right>") : rText) +
                  "'); convert one side explicitly (e.g. parseTo('it.length', number))");
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
        s.tupleElems = tupleElems;  // copied: the type below keeps them too
        shapes_.emplace(label, std::move(s));
    }
    Type t(TypeKind::Object, label);
    // A tuple's element types ride in `parts` so downstream consumers (codegen)
    // can classify the tuple without decoding the shape label.
    if (tuple) t.parts = std::move(tupleElems);
    return t;
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
// The Omit operation (the rest object of a destructuring pattern)
// =============================================================================
// `let { a, ...rest } = obj` types `rest` as the source's shape MINUS the
// properties the pattern named (TypeScript's Omit). The omitted shape is a
// shape of its own: the kept fields, in the order the source declared them.
//
// A source without an enumerable shape has no Omit: an open shape (ambient)
// accepts any property, a class is nominal, and a primitive is not an object
// at all. Those keep the `unknown` type, and the use site reports why.
Type Analyzer::omitShape(const Type& source,
                         const std::vector<std::string>& named) {
    const Shape* sh = shapeOf(source);
    if (sh == nullptr || sh->open) return Type::unknown();
    std::vector<Field> kept;
    for (const Field& f : sh->fields) {
        if (std::find(named.begin(), named.end(), f.name) != named.end()) {
            continue;
        }
        kept.push_back(f);
    }
    return makeObjectType(std::move(kept), false, false, {});
}

// =============================================================================
// Built-in utility types: Partial / Required / Readonly / Pick / Omit / Record
// =============================================================================
// The transforms below are pure over the resolved shape's field list (see
// `Analyzer::resolveNamedType`, which applies them and publishes the result
// through the ordinary `resolveType` side table - codegen reads the resolved
// shape exactly like any other object shape; there is no runtime metadata).
// Full design: docs/design/utility-types.md.

namespace {

// The field names a Pick/Omit/Record key type stands for: a string (or
// integral number) literal, or a union of them. False when the key type is
// anything else (`string` without a literal, a type parameter, ...): the
// caller reports CP3401 and keeps `unknown`.
bool utilityKeysOf(const Type& keys, std::vector<std::string>& out) {
    out.clear();
    const auto one = [&out](const Type& t) -> bool {
        if (t.isLiteral() && t.literalBase == TypeKind::String) {
            out.push_back(t.stringValue);
            return true;
        }
        if (t.isLiteral() && t.literalBase == TypeKind::Number) {
            const double v = t.numberValue;
            if (v == static_cast<double>(static_cast<long long>(v))) {
                out.push_back(std::to_string(static_cast<long long>(v)));
                return true;
            }
        }
        return false;
    };
    if (keys.isUnion()) {
        for (const Type& m : keys.parts) {
            if (!one(m)) return false;
        }
        return true;
    }
    return one(keys);
}

// True when `keys` is a union whose every member is a string/number literal
// (the closed `Record<"a" | "b", V>` form). A single literal is *not* closed:
// it canonicalizes to `Map<string, V>` like `Record<string, V>` does.
bool isClosedLiteralKeys(const Type& keys) {
    if (!keys.isUnion() || keys.parts.empty()) return false;
    std::vector<std::string> tmp;
    return utilityKeysOf(keys, tmp);
}

}  // namespace

// =============================================================================
// Type resolution
// =============================================================================
// Every resolved type annotation is published into the `SemaResult` side table
// (D1), so codegen can look a `TypeNode`'s resolved type up by node id
// (`sema.typeOf`) instead of re-deriving it structurally - the resolved model
// carries object shapes and generic arguments (`Thread<In, Out>`) a structural
// re-parse cannot reconstruct.
Type Analyzer::resolveType(TypeNode& node) {
    return withType(node, resolveTypeInner(node));
}

Type Analyzer::resolveTypeInner(TypeNode& node) {
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

// The primitive a type name spells (`number`, `string`, `boolean`, `void`,
// `undefined`, `null`, `unknown`, `never`, `object`). False when the name is
// not one of them.
bool Analyzer::primitiveTypeOf(const std::string& name, Type& out) {
    if (name == "number") { out = Type::number(); return true; }
    if (name == "string") { out = Type::string(); return true; }
    if (name == "boolean") { out = Type::boolean(); return true; }
    if (name == "undefined") { out = Type::undefined(); return true; }
    if (name == "null") { out = Type::nullType(); return true; }
    if (name == "void") { out = Type::voidType(); return true; }
    if (name == "object") { out = ambient(); return true; }
    if (name == "unknown" || name == "never") { out = Type::unknown(); return true; }
    return false;
}

// The instantiated shape of a generic interface (D26): the type arguments are
// substituted into the interface's members, so `Box<number>` is the shape
// `{ value: number }`.
Type Analyzer::resolveGenericInterface(const std::string& name,
                                       const std::vector<Type>& args) {
    const auto& generic = genericInterfaces_.at(name);
    std::vector<Field> fields = generic.fields;
    for (Field& f : fields) {
        f.type = substituteTypeParams(f.type, generic.typeParams, args);
    }
    return makeObjectType(std::move(fields), generic.open, false, {});
}

Type Analyzer::resolveNamedType(NamedType& nt) {
    // Type arguments are resolved eagerly (both for their own diagnostics and
    // for the generic types M1 does model: `Promise<T>`, the concurrency
    // channel types and user classes).
    std::vector<Type> args;
    for (auto& ta : nt.typeArgs) {
        if (!ta) continue;
        args.push_back(resolveType(*ta));
    }
    const bool hasArg = !args.empty();
    const Type firstArg = hasArg ? args.front() : Type::unknown();
    // -------------------------------------------------------------------------
    // Generic constraints/defaults (`<T extends X = Y>`, ROADMAP L "Generic
    // constraints and defaults"): instantiation checking against
    // `GenericParamSpec` (cp3/core/utility_types.hpp). See
    // docs/design/generic-constraints.md.
    //
    // Encoding contract for a resolved `TypeKind::TypeParam` (the producer
    // side - `registerTypeParams` / `enterClassTypeParams`, outside this
    // file's allowed set - must honour it): `parts[0]`, when present, is the
    // resolved `extends` constraint (the pre-existing convention, already
    // used by generic function calls in generic_check.cpp), and `parts[1]`,
    // when present, is the resolved `= Y` default. `GenericParamSpec` is
    // the wider record that also carries the parameter name and the
    // hasConstraint/hasDefault flags, so the check below is written once
    // against the spec instead of re-deriving it per declaration kind.
    //
    // The specs for an alias/interface/class are recovered here from the
    // `TypeParam` leaves of its already-resolved body (alias body,
    // interface fields, class member/signature types): a leaf names its
    // parameter and carries its constraint/default in the slots above.
    // A parameter that never occurs in the body carries no leaf to recover
    // from and is therefore unchecked - the producer must store the specs
    // beside the name lists to close that gap (design doc, "Producer
    // contract"); this consumer is what activates the moment the leaves
    // carry the slots.
    // -------------------------------------------------------------------------
    auto collectSpecs = [&](const std::vector<std::string>& names,
                            const std::vector<Type>& bodies) {
        std::vector<GenericParamSpec> specs;
        specs.reserve(names.size());
        for (const std::string& n : names) {
            GenericParamSpec s;
            s.name = n;
            specs.push_back(std::move(s));
        }
        std::function<void(const Type&)> walk = [&](const Type& t) {
            if (t.kind == TypeKind::TypeParam) {
                for (std::size_t i = 0; i < names.size(); ++i) {
                    if (names[i] != t.name) continue;
                    if (!t.parts.empty() && !t.parts.front().isUnknown() &&
                        !specs[i].hasConstraint) {
                        specs[i].constraint = t.parts.front();
                        specs[i].hasConstraint = true;
                    }
                    if (t.parts.size() >= 2 && !t.parts[1].isUnknown() &&
                        !specs[i].hasDefault) {
                        specs[i].defaultType = t.parts[1];
                        specs[i].hasDefault = true;
                    }
                }
            }
            if (t.kind == TypeKind::Object) {
                Shape* sh = shapeOf(t);
                if (sh != nullptr) {
                    if (sh->tuple) {
                        for (const Type& e : sh->tupleElems) walk(e);
                    } else {
                        for (const Field& f : sh->fields) walk(f.type);
                    }
                }
            }
            for (const Type& p : t.parts) walk(p);
        };
        for (const Type& b : bodies) walk(b);
        return specs;
    };
    // True when `t` mentions the type parameter `name` (a self-referential
    // constraint such as `T extends Box<T>` cannot be decided by
    // `wouldAccept` and is treated permissively).
    auto mentionsParam = [&](const Type& t, const std::string& name) {
        std::function<bool(const Type&)> has = [&](const Type& x) {
            if (x.kind == TypeKind::TypeParam && x.name == name) return true;
            if (x.kind == TypeKind::Object) {
                Shape* sh = shapeOf(x);
                if (sh != nullptr) {
                    if (sh->tuple) {
                        for (const Type& e : sh->tupleElems)
                            if (has(e)) return true;
                    } else {
                        for (const Field& f : sh->fields)
                            if (has(f.type)) return true;
                    }
                }
            }
            for (const Type& p : x.parts)
                if (has(p)) return true;
            return false;
        };
        return has(t);
    };
    // Checks the provided arguments against the specs (CP3412 on a
    // violation) and fills the missing ones from their defaults into
    // `effArgs` (which starts as the provided list). Returns false when any
    // provided argument violates its constraint, in which case the caller
    // resolves the instantiation to `unknown` (fail-closed, mirroring the
    // excess-arity CP3418 legs). Only *provided* arguments are checked: a
    // default that violates its own constraint is a declaration-site error
    // (the producer's job), and reporting it at every use site would point
    // at the wrong span.
    auto checkInstantiation =
        [&](const std::string& kind, const std::string& declName,
            const std::vector<GenericParamSpec>& specs,
            std::vector<Type>& effArgs) -> bool {
        const std::size_t provided = effArgs.size();
        std::unordered_map<std::string, Type> known;
        for (std::size_t i = 0; i < specs.size(); ++i) {
            if (i < effArgs.size()) {
                known[specs[i].name] = effArgs[i];
            } else if (specs[i].hasDefault) {
                // A default may mention earlier parameters
                // (`<T, U extends T = T>`); substitute what is known.
                Type d = substituteType(specs[i].defaultType, known);
                known[specs[i].name] = d;
                effArgs.push_back(std::move(d));
            }
        }
        bool ok = true;
        const std::size_t checked =
            provided < specs.size() ? provided : specs.size();
        for (std::size_t i = 0; i < checked; ++i) {
            const GenericParamSpec& spec = specs[i];
            if (!spec.hasConstraint) continue;
            const Type& arg = effArgs[i];
            // Cascade suppression, mirroring generic_check.cpp: an
            // unknown/error/ambient argument or constraint carries no
            // information, and a type-parameter argument is accepted by
            // `wouldAccept` unconditionally (its own constraint is not
            // compared here).
            if (arg.isUnknown() || arg.isError() || isAmbientType(arg))
                continue;
            std::unordered_map<std::string, Type> earlier;
            for (std::size_t j = 0; j < i; ++j) {
                earlier[specs[j].name] = effArgs[j];
            }
            Type constraint = substituteType(spec.constraint, earlier);
            if (constraint.isUnknown() || isAmbientType(constraint)) continue;
            if (mentionsParam(constraint, spec.name)) continue;
            if (wouldAccept(arg, constraint, false)) continue;
            ok = false;
            std::string cname = pretty(constraint);
            auto spelled = typeParamConstraintNames_.find(typeKey(constraint));
            if (spelled != typeParamConstraintNames_.end()) {
                cname = spelled->second;
            }
            report(diagcode::kConstraintNotSatisfied, nt.span,
                   "type argument '" + pretty(arg) +
                       "' does not satisfy the constraint '" + cname +
                       "' of type parameter '" + spec.name + "' in " + kind +
                       " '" + declName + "'");
        }
        return ok;
    };
    // The concurrency channel types (D80, spec 13): ambient generic classes.
    // `Thread<In, Out>` types the worker entry's parameter; `Worker<In, Out>`
    // is the main side's handle type. Both are nominal Class types whose parts
    // are the resolved direction types, in order - the single source of truth
    // codegen reads back through the type-node side table. `undefined` in a
    // direction spells an empty direction: the direction can never carry a
    // value, so sends into it are a compile error and its listener is nullary.
    // Codegen keys the empty direction off the void sentinel (`ThreadHandle<T,
    // void>` -> `VoidSlot`), which the retired `void` spelling used to supply.
    if (nt.name == "Thread" || nt.name == "Worker") {
        Type channel(TypeKind::Class, nt.name);
        channel.parts.reserve(args.size());
        for (const Type& arg : args) {
            channel.parts.push_back(arg.isUndefined() ? Type::voidType() : arg);
        }
        return channel;
    }
    if (nt.name == "any") {
        checkBannedAny(nt.span);
        return ambient();
    }
    // Ledger K (docs/design/remove-void-unknown.md): the `void` and
    // `unknown` spellings are retired as annotations. `void` always means
    // "this returns nothing" -> the language says `undefined`; an untyped
    // value is spelled `object`, and anything else takes the concrete type.
    // This is the one choke point for every annotation position (variables,
    // parameters, return types, type aliases, type arguments, ...) because
    // resolveType recurses through union/array/tuple/function/object
    // nodes into resolveNamedType, so nested spellings (`Promise<void>`,
    // `void[]`, `unknown | null`) are caught by recursion. It fires on the
    // source spelling only: the internal `Type::unknown()` /
    // `Type::voidType()` sentinels (errors, inference) are untouched, so
    // nothing cascades. `void` resolves to `undefined` so a misannotated
    // program still type-checks against the intended type (and codegen
    // sees a representable type); `unknown` keeps the unknown sentinel
    // for the same reason (the error is already reported). Precedence
    // matches the old primitive lookup below: a user type or type
    // parameter named `void`/`unknown` was already unnameable (the keyword
    // token wins), so no behaviour changes beyond the diagnostic.
    if (nt.name == "void") {
        report(diagcode::kVoidTypeBanned, nt.span,
               "the 'void' type is removed; annotate the return as "
               "'undefined' instead");
        return Type::undefined();
    }
    if (nt.name == "unknown") {
        report(diagcode::kUnknownTypeBanned, nt.span,
               "the 'unknown' type is removed; use a concrete type, or "
               "'object' for an untyped value");
        return Type::unknown();
    }
    Type primitive;
    if (primitiveTypeOf(nt.name, primitive)) return primitive;
    // `Promise<T>` is the one built-in generic (spec §14): a nominal class type
    // carrying its value type. A bare `Promise` is `Promise<unknown>`.
    if (nt.name == "Promise") {
        return promiseOf(hasArg ? firstArg : Type::unknown());
    }
    // `Array<T>` is the generic spelling of `T[]` (docs/standard-library §03
    // accepts the `Array<number>(n)` value form; the type form is the same
    // element rule). One type argument is required; `T[]` is the same type.
    if (nt.name == "Array") {
        if (!hasArg) {
            report(diagcode::kInvalidTypeArgument, nt.span,
                   "'Array' takes exactly one type argument; write "
                   "'Array<T>' or 'T[]'");
            return Type::array(Type::unknown());
        }
        if (args.size() > 1) {
            report(diagcode::kInvalidTypeArgument, nt.span,
                   "'Array' takes exactly one type argument; got " +
                       std::to_string(args.size()));
        }
        return Type::array(firstArg);
    }
    // The nominal `Buffer` of std/buffer (docs/standard-library/04 §buffer):
    // the annotation resolves only while the `import { Buffer } from
    // "std/buffer"` binding is in scope, so the name never leaks into a
    // program that does not import the module.
    if (isByteBufferTypeName(nt.name)) {
        Binding b;
        if (lookup(nt.name, b) && b.isAmbient) return byteBufferType();
    }
    if (isTextTypeName(nt.name)) {
        Binding b;
        if (lookup(nt.name, b) && b.isAmbient)
            return Type::named(TypeKind::Class, nt.name);
    }

    // A type alias names a type: a generic alias substitutes its type
    // arguments into the aliased type (`Pair<number>` is `[number, number]`).
    // Instantiation is checked here: arity (CP3418), then constraints
    // (CP3412) and default-fill against the recovered `GenericParamSpec`s,
    // then substitution. A missing argument with no default keeps today's
    // permissive bare-parameter behaviour.
    if (typeAliasParams_.count(nt.name) != 0) {
        const auto& params = typeAliasParams_.at(nt.name);
        if (args.size() > params.size()) {
            report(diagcode::kInvalidTypeArgument, nt.span,
                   "type alias '" + nt.name + "' expects " +
                       std::to_string(params.size()) +
                       " type argument(s); got " +
                       std::to_string(args.size()));
            return Type::unknown();
        }
        const auto baseIt = typeNames_.find(nt.name);
        if (baseIt == typeNames_.end()) return Type::unknown();
        const std::vector<GenericParamSpec> specs =
            collectSpecs(params, {baseIt->second});
        std::vector<Type> effArgs = args;
        if (!checkInstantiation("type alias", nt.name, specs, effArgs)) {
            return Type::unknown();
        }
        // Shape-aware substitution: `substituteTypeParams` (declarations.cpp)
        // only walks `Type::parts`, so a generic alias whose body is an
        // object shape (`type Box<T> = { value: T }`) would keep its bare
        // `T`. This walk also substitutes inside object/tuple shapes, which
        // is what makes `Box<number>` the shape `{ value: number }`.
        std::function<Type(const Type&)> subst = [&](const Type& t) -> Type {
            if (t.kind == TypeKind::TypeParam) {
                for (std::size_t i = 0; i < params.size(); ++i) {
                    if (params[i] != t.name) continue;
                    if (i < effArgs.size()) return effArgs[i];
                    return t;  // missing argument: permissive bare parameter
                }
                return t;
            }
            if (t.kind == TypeKind::Object) {
                Shape* sh = shapeOf(t);
                if (!sh) return t;
                if (sh->tuple) {
                    std::vector<Type> elems;
                    elems.reserve(sh->tupleElems.size());
                    for (const Type& e : sh->tupleElems) elems.push_back(subst(e));
                    return makeObjectType({}, false, true, std::move(elems));
                }
                if (sh->open || sh->fields.empty()) return t;
                std::vector<Field> fields;
                fields.reserve(sh->fields.size());
                for (const Field& f : sh->fields) {
                    fields.push_back(Field{f.name, subst(f.type), f.optional});
                }
                return makeObjectType(std::move(fields), false, false, {});
            }
            Type out = t;
            for (Type& part : out.parts) part = subst(part);
            return out;
        };
        return subst(baseIt->second);
    }
    // A generic interface (D26): the type arguments are substituted into the
    // interface's members, so `Box<number>` is the shape `{ value: number }`.
    // Instantiation is checked here first: arity (CP3418), then constraints
    // (CP3412) and default-fill against the recovered specs.
    if (genericInterfaces_.count(nt.name) != 0) {
        const auto& gi = genericInterfaces_.at(nt.name);
        if (args.size() > gi.typeParams.size()) {
            report(diagcode::kInvalidTypeArgument, nt.span,
                   "interface '" + nt.name + "' expects " +
                       std::to_string(gi.typeParams.size()) +
                       " type argument(s); got " +
                       std::to_string(args.size()));
            return Type::unknown();
        }
        std::vector<Type> bodies;
        bodies.reserve(gi.fields.size());
        for (const Field& f : gi.fields) bodies.push_back(f.type);
        const std::vector<GenericParamSpec> specs =
            collectSpecs(gi.typeParams, bodies);
        std::vector<Type> effArgs = args;
        if (!checkInstantiation("interface", nt.name, specs, effArgs)) {
            return Type::unknown();
        }
        return resolveGenericInterface(nt.name, effArgs);
    }
    const auto it = typeNames_.find(nt.name);
    if (it != typeNames_.end()) {
        Type base = it->second;
        // A user class instantiation (`Box<number>`): the type arguments are
        // carried in `parts` for member substitution and codegen spelling.
        // An excess argument list for a known generic class is CP3418; its
        // provided arguments are then checked against the recovered specs
        // (CP3412) and missing ones filled from defaults, so a bare
        // `Box` for `class Box<T = number>` carries `number`. A class the
        // unit does not know keeps the permissive fallthrough.
        if (base.kind == TypeKind::Class) {
            if (const ClassInfo* ci = findClass(nt.name)) {
                if (args.size() > ci->typeParamNames.size()) {
                    report(diagcode::kInvalidTypeArgument, nt.span,
                           "class '" + nt.name + "' expects " +
                               std::to_string(ci->typeParamNames.size()) +
                               " type argument(s); got " +
                               std::to_string(args.size()));
                    return Type::unknown();
                }
                std::vector<Type> bodies;
                for (const ClassMemberInfo& m : ci->members) {
                    bodies.push_back(m.type);
                    for (const Type& p : m.sig.params) bodies.push_back(p);
                    bodies.push_back(m.sig.ret);
                }
                for (const Type& p : ci->ctorSig.params) bodies.push_back(p);
                bodies.push_back(ci->ctorSig.ret);
                const std::vector<GenericParamSpec> specs =
                    collectSpecs(ci->typeParamNames, bodies);
                std::vector<Type> effArgs = args;
                if (!checkInstantiation("class", nt.name, specs, effArgs)) {
                    return Type::unknown();
                }
                if (!effArgs.empty()) base.parts = effArgs;
                return base;
            }
            if (!args.empty()) base.parts = args;
        }
        return base;
    }
    // The built-in utility types (Partial / Required / Readonly / Pick /
    // Omit / Record). The placement after the user alias/interface/type-name
    // lookups above means a user-declared `Partial` etc. shadows the
    // builtin, as for every other name (and as `Record` already behaved).
    // Every utility resolves *structurally* to a plain `Type` - an object
    // shape (or, for the open `Record<string, V>` form, the canonical
    // `Map<string, V>` of docs/design/record-index-signatures.md) - so the
    // resolved type is published through the ordinary `resolveType` side
    // table and codegen needs no utility-specific lowering. Full design:
    // docs/design/utility-types.md.
    if (const std::optional<UtilityKind> uk = utilityKindOf(nt.name)) {
        const UtilityKind kind = *uk;
        const std::size_t want = utilityArity(kind);
        if (args.size() != want) {
            report(diagcode::kInvalidTypeArgument, nt.span,
                   std::string(utilityKindName(kind)) +
                       " requires exactly " + std::to_string(want) +
                       " type argument(s); got " +
                       std::to_string(args.size()));
            return Type::unknown();
        }
        // `Record` is the odd one out: its first argument is a *key* type,
        // not a source object type.
        if (kind == UtilityKind::Record) {
            const Type& keys = args[0];
            const Type& value = args[1];
            // Closed form: `Record<"a" | "b", V>` is the object shape with
            // exactly those fields, each of type V (TypeScript semantics).
            if (isClosedLiteralKeys(keys)) {
                std::vector<std::string> names;
                (void)utilityKeysOf(keys, names);
                std::vector<Field> fields;
                fields.reserve(names.size());
                for (const std::string& n : names) {
                    fields.push_back(Field{n, value, false});
                }
                return makeObjectType(std::move(fields), false, false, {});
            }
            // Open form: `Record<string, V>` canonicalizes to `Map<string,
            // V>` (see docs/design/record-index-signatures.md). Only a
            // string key is a record; a non-string key is the dynamic-key
            // dictionary CP3803 already bans for index signatures, and
            // `Map<K,V>` is its supported form. A type parameter / unknown
            // / error key is deferred (cascade suppression, as elsewhere
            // in this function) rather than diagnosed here.
            if (!isStringish(keys) && !keys.isUnknown() && !keys.isError() &&
                keys.kind != TypeKind::TypeParam) {
                report(diagcode::kIndexSignatureBanned, nt.span,
                       "Record<K, V> requires K to be string; use Map<K, V> for "
                       "non-string keys");
                return Type::unknown();
            }
            Type record(TypeKind::Class, "Map");
            record.parts = {Type::string(), value};
            return record;
        }
        // Partial / Required / Readonly / Pick / Omit over a source type.
        const Type source = args[0];
        std::vector<std::string> keys;
        if (kind == UtilityKind::Pick || kind == UtilityKind::Omit) {
            if (!utilityKeysOf(args[1], keys)) {
                report(diagcode::kTypeMismatch, nt.span,
                       std::string(utilityKindName(kind)) +
                           " keys must be string literal types (for example "
                           "\"a\" | \"b\")");
                return Type::unknown();
            }
        }
        // Applies the field transform to one (non-union) source type.
        // `transform` returns the resolved type for any source it can
        // enumerate; anything else falls through to the permissive cases
        // below. Class sources are enumerated from their member table
        // (instance members only; methods keep their function type).
        const auto transformFields = [&](std::vector<Field> fields) -> Type {
            switch (kind) {
                case UtilityKind::Partial:
                    for (Field& f : fields) f.optional = true;
                    return makeObjectType(std::move(fields), false, false, {});
                case UtilityKind::Required:
                    for (Field& f : fields) f.optional = false;
                    return makeObjectType(std::move(fields), false, false, {});
                case UtilityKind::Readonly: {
                    // Readonly keeps the same fields and optionality but is
                    // a *distinct* type from the mutable shape: the label
                    // carries an `$ro` prefix so `Type::operator==`
                    // distinguishes them while assignability (which walks
                    // the shape fields) treats them as mutually assignable,
                    // TypeScript-style. The readonly *write* rejection is a
                    // member-assignment concern; see the design doc §4.
                    std::string label = "$ro{";
                    for (const Field& f : fields) {
                        label += f.name;
                        label += f.optional ? "?" : "";
                        label += ':';
                        label += typeKey(f.type);
                        label += ',';
                    }
                    label += '}';
                    if (shapes_.find(label) == shapes_.end()) {
                        Shape s;
                        s.fields = fields;
                        shapes_.emplace(label, std::move(s));
                    }
                    return Type(TypeKind::Object, label);
                }
                case UtilityKind::Pick: {
                    std::vector<Field> kept;
                    for (const Field& f : fields) {
                        if (std::find(keys.begin(), keys.end(), f.name) !=
                            keys.end()) {
                            kept.push_back(f);
                        }
                    }
                    return makeObjectType(std::move(kept), false, false, {});
                }
                case UtilityKind::Omit: {
                    std::vector<Field> kept;
                    for (const Field& f : fields) {
                        if (std::find(keys.begin(), keys.end(), f.name) ==
                            keys.end()) {
                            kept.push_back(f);
                        }
                    }
                    return makeObjectType(std::move(kept), false, false, {});
                }
                case UtilityKind::Record:
                    break;  // handled above
            }
            return Type::unknown();
        };
        std::function<Type(const Type&)> apply =
            [&](const Type& src) -> Type {
            if (src.isUnion()) {
                std::vector<Type> members;
                members.reserve(src.parts.size());
                for (const Type& m : src.parts) members.push_back(apply(m));
                return Type::unionOf(std::move(members));
            }
            if (src.isNullable()) {
                const Type inner =
                    src.parts.empty() ? Type::unknown() : src.parts.front();
                return Type::nullable(apply(inner));
            }
            if (src.kind == TypeKind::TypeParam) {
                // A type parameter stands for its constraint: transform the
                // constraint when it is an enumerable shape, otherwise keep
                // the bare parameter (permissive, as elsewhere).
                if (!src.parts.empty()) {
                    const Type& c = src.parts.front();
                    if (Shape* cs = shapeOf(c)) {
                        if (!cs->open && !cs->tuple) {
                            return transformFields(cs->fields);
                        }
                    }
                }
                return src;
            }
            if (src.kind == TypeKind::Class) {
                if (const ClassInfo* ci = findClass(src.name)) {
                    std::vector<Field> fields;
                    for (const ClassMemberInfo& m : ci->members) {
                        if (m.isStatic) continue;
                        if (m.isMethod) {
                            fields.push_back(Field{m.name,
                                                   Type::function(m.sig.params,
                                                                  m.sig.ret),
                                                   m.optional});
                        } else {
                            // Instantiate the member type first, so
                            // `Partial<Box<number>>` sees `number`, not `T`.
                            const Type mt = src.parts.empty()
                                                ? m.type
                                                : substituteClassTypeParams(
                                                      m.type, *ci, src.parts);
                            fields.push_back(Field{m.name, mt, m.optional});
                        }
                    }
                    return transformFields(std::move(fields));
                }
                // Nominal class the unit does not know: no enumerable shape.
                return Type::unknown();
            }
            Shape* sh = shapeOf(src);
            if (sh != nullptr) {
                if (sh->tuple) {
                    // Tuple elements have no per-element optionality in the
                    // shape model, so Partial/Required/Readonly keep the
                    // tuple as-is (documented limitation); Pick/Omit over a
                    // tuple have no field set to select from.
                    if (kind == UtilityKind::Pick ||
                        kind == UtilityKind::Omit) {
                        return Type::unknown();
                    }
                    return src;
                }
                if (sh->open) {
                    // Open (ambient/computed) shapes accept any property:
                    // the member set is unchanged by Partial/Required/
                    // Readonly, and unknowable by Pick/Omit.
                    if (kind == UtilityKind::Pick ||
                        kind == UtilityKind::Omit) {
                        return Type::unknown();
                    }
                    return src;
                }
                return transformFields(sh->fields);
            }
            // Arrays, primitives and everything else pass through untouched:
            // `Partial<number>` is `number` in TypeScript too.
            return src;
        };
        return apply(source);
    }
    // A non-TypeScript project also accepts its own plain value-type names:
    // `let a: i64 = 10;` (rust) or `let a: long long = 10;` (cpp). Unsupported
    // spellings (i128, long double, ...) get their precise diagnostic; a name
    // that is simply not native falls through to the ordinary undefined-type
    // error below.
    if (options_.typeSystem != TypeSystem::TypeScript) {
        NativeNameResult native =
            resolvePlainNativeTypeName(options_.typeSystem, nt.name);
        if (native.type) return *native.type;
        if (native.status == NativeNameStatus::Unsupported) {
            report(diagcode::kUnknownType, nt.span, native.error);
            return Type::unknown();
        }
    }
    report(diagcode::kUndefinedType, nt.span,
           "cannot find type name '" + nt.name + "'");
    return Type::unknown();
}

Type Analyzer::resolveUnionType(UnionType& ut) {
    std::vector<Type> members;
    for (auto& t : ut.types) {
        if (t) members.push_back(resolveType(*t));
    }
    Type result = Type::unionOf(std::move(members));
    rejectNativeUnion(result, ut.span);
    return result;
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
