// =============================================================================
// src/language/typed-arrays/sema.cpp - the JS-compat typed-array family (D69).
//
// Owns the family model for `Array` extras plus the 11 typed classes, the two
// buffer classes and DataView: the constructor grammar (`inferFamilyConstruct`
// and its per-class helpers), the indexed-read dispatch, and the declarator
// checks for `Array(n)`. The shared value helpers (`constantNumber`,
// `knownTypedLength`) and the in-range index proof live in the sibling
// `sema_index.cpp`. The `Array.of` / `Array.from` statics, the `inferCall`
// hooks and the member tables live in `sema_methods.cpp`.
//
// Family types are nominal `TypeKind::Class` values with no extra metadata;
// codegen owns the C++ mapping (cp3/typed_array.hpp). The member checks here
// only describe the JS surface the corpus pins (the section 6 case list is the
// oracle).
// =============================================================================
#include "detail/analyzer.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace {

constexpr const char* kTypedArrayNames[] = {
    "Int8Array",   "Uint8Array", "Uint8ClampedArray", "Int16Array",
    "Uint16Array", "Int32Array", "Uint32Array",       "Float32Array",
    "Float64Array", "BigInt64Array", "BigUint64Array",
};

}  // namespace

// =============================================================================
// The family tables (shared with scopes.cpp's builtin registration)
// =============================================================================
bool isTypedArrayName(const std::string& name) {
    for (const char* n : kTypedArrayNames) {
        if (name == n) return true;
    }
    return false;
}

bool isTypedArrayClass(const Type& t) {
    return t.kind == TypeKind::Class && isTypedArrayName(t.name);
}

bool isBufferClass(const Type& t) {
    return t.kind == TypeKind::Class &&
           (t.name == "ArrayBuffer" || t.name == "SharedArrayBuffer");
}

bool isDataViewClass(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "DataView";
}

bool isFamilyCtorName(const std::string& name) {
    return name == "Array" || isTypedArrayName(name) || name == "ArrayBuffer" ||
           name == "SharedArrayBuffer" || name == "DataView";
}

const std::vector<std::string>& familyTypeNames() {
    static const std::vector<std::string> kNames = [] {
        std::vector<std::string> names;
        for (const char* n : kTypedArrayNames) names.emplace_back(n);
        names.emplace_back("ArrayBuffer");
        names.emplace_back("SharedArrayBuffer");
        names.emplace_back("DataView");
        return names;
    }();
    return kNames;
}

namespace {

// A literal length argument: a number literal, or a negated number literal.
// `NaN` and `Infinity` are identifiers, so they never take these
// compile-time paths.
bool literalLengthValue(const Expr& e, double& out) {
    if (e.kind == NodeKind::NumberLiteral) {
        out = static_cast<const NumberLiteral&>(e).value;
        return true;
    }
    if (e.kind == NodeKind::UnaryExpr) {
        const auto& u = static_cast<const UnaryExpr&>(e);
        if (u.op == UnaryOp::Negate && u.operand &&
            u.operand->kind == NodeKind::NumberLiteral) {
            out = -static_cast<const NumberLiteral&>(*u.operand).value;
            return true;
        }
    }
    return false;
}

// `Array(n)` default-initializes every element; only value-like types have a
// default in cp3, so reference element types (arrays, classes, objects) must
// be filled instead.
bool isDefaultInitializable(const Type& t) {
    if (isPermissive(t)) return true;
    if (t.isLiteral()) return isDefaultInitializable(t.literalBaseType());
    if (t.isNumber() || t.isString() || t.isBoolean() || t.isNull() ||
        t.isUndefined()) {
        return true;
    }
    if (t.isNullable()) {
        return t.parts.empty() || isDefaultInitializable(t.parts.front());
    }
    if (t.isUnion()) {
        for (const Type& m : t.parts) {
            if (!isDefaultInitializable(m)) return false;
        }
        return true;
    }
    return false;
}

// The statically evident element count of a typed-array expression: a literal
// length, a literal element list, a copy of another known view, or the result
// of a length-preserving method on one. Anything else has no known length, so
// a literal index is not provably in range.
std::optional<double> knownTypedLength(const Expr& e) {
    const Expr* callee = nullptr;
    const ExprList* args = nullptr;
    if (e.kind == NodeKind::NewExpr) {
        const auto& n = static_cast<const NewExpr&>(e);
        callee = n.callee.get();
        args = &n.arguments;
    } else if (e.kind == NodeKind::CallExpr) {
        const auto& c = static_cast<const CallExpr&>(e);
        callee = c.callee.get();
        args = &c.arguments;
    } else {
        return std::nullopt;
    }
    if (!callee || !args) return std::nullopt;
    if (callee->kind == NodeKind::Identifier) {
        if (!isTypedArrayName(static_cast<const Identifier&>(*callee).name)) {
            return std::nullopt;
        }
        if (args->size() == 1 && args->front()) {
            const Expr& a0 = *args->front();
            double lit = 0.0;
            if (literalLengthValue(a0, lit)) {
                return lit >= 0.0 ? std::optional<double>(std::trunc(lit))
                                  : std::nullopt;
            }
            if (a0.kind == NodeKind::ArrayLiteral) {
                return static_cast<double>(
                    static_cast<const ArrayLiteral&>(a0).elements.size());
            }
            return knownTypedLength(a0);  // a copy keeps the source's count
        }
        if (args->size() == 3 && args->back()) {
            double lit = 0.0;
            if (literalLengthValue(*args->back(), lit) && lit >= 0.0) {
                return std::trunc(lit);
            }
        }
        return std::nullopt;
    }
    if (callee->kind == NodeKind::MemberExpr) {
        const auto& m = static_cast<const MemberExpr&>(*callee);
        if (m.object && (m.property == "fill" || m.property == "reverse" ||
                         m.property == "copyWithin")) {
            return knownTypedLength(*m.object);
        }
    }
    return std::nullopt;
}

// The `Array(n)` length form of a declarator initializer. A reported error
// never reaches here: the sema type is exactly `unknown[]` only for the
// length form.
bool isArrayLengthForm(const Expr& init, const Type& initType) {
    if (initType.kind != TypeKind::Array) return false;
    if (!initType.parts.empty() && !initType.parts.front().isUnknown()) {
        return false;
    }
    const Expr* callee = nullptr;
    std::size_t count = 0;
    if (init.kind == NodeKind::CallExpr) {
        const auto& c = static_cast<const CallExpr&>(init);
        callee = c.callee.get();
        count = c.arguments.size();
    } else if (init.kind == NodeKind::NewExpr) {
        const auto& n = static_cast<const NewExpr&>(init);
        callee = n.callee.get();
        count = n.arguments.size();
    } else {
        return false;
    }
    return callee && callee->kind == NodeKind::Identifier && count == 1 &&
           static_cast<const Identifier&>(*callee).name == "Array";
}

}  // namespace

// =============================================================================
// Constructors
// =============================================================================
Type Analyzer::inferFamilyConstruct(const std::string& cls,
                                    const TypeList& typeArgs,
                                    ExprList& args,
                                    const SourceSpan& span) {
    if (cls == "Array" && !typeArgs.empty()) {
        // `Array<number>(3)`: an explicit element type argument — TS's own
        // spelling — avoids the unknown-element Array(n) ambiguity. Members
        // take at most one element type argument.
        if (typeArgs.size() != 1 || !typeArgs.front()) {
            report(diagcode::kArityMismatch, span,
                   "arity mismatch: Array takes one type argument");
            return Type::error();
        }
        return Type::array(resolveType(*typeArgs.front()));
    }
    std::vector<Type> types;
    types.reserve(args.size());
    for (auto& a : args) {
        if (a && a->kind == NodeKind::SpreadElement) {
            auto& sp = static_cast<SpreadElement&>(*a);
            Type at = sp.argument ? inferExpr(*sp.argument) : Type::unknown();
            // A spread contributes its *elements* to the constructed array.
            types.push_back(at.kind == TypeKind::Array ? elemOf(at)
                                                        : Type::unknown());
            continue;
        }
        types.push_back(a ? inferExpr(*a) : Type::unknown());
    }
    if (cls == "Array") return inferArrayConstruct(args, types, span);
    if (cls == "ArrayBuffer" || cls == "SharedArrayBuffer") {
        return inferBufferConstruct(cls, args, types, span);
    }
    if (cls == "DataView") return inferDataViewConstruct(args, types, span);
    return inferTypedViewConstruct(cls, args, types, span);
}

Type Analyzer::inferArrayConstruct(const ExprList& args,
                                   const std::vector<Type>& types,
                                   const SourceSpan& span) {
    // A spread argument contributes its *elements* (the literal form, JS parity).
    if (args.size() == 1 && args.front() && args.front()->kind != NodeKind::SpreadElement) {
        double lit = 0.0;
        if (args.front() && literalLengthValue(*args.front(), lit)) {
            if (lit < 0.0 || std::trunc(lit) != lit) {
                report(diagcode::kTypeMismatch, span,
                       "Array length must be a non-negative integer");
                return Type::error();
            }
            return Type::array(Type::unknown());
        }
        if (isNumberish(types[0]) || isPermissive(types[0])) {
            return Type::array(Type::unknown());
        }
        report(diagcode::kTypeMismatch, span,
               "type mismatch: a single non-number argument is never an array "
               "length; write an array literal [x] instead");
        return Type::error();
    }
    // The element type: a spread argument contributes its *element* type.
    Type elem = Type::unknown();
    for (std::size_t i = 0; i < types.size(); ++i) {
        Type t = types[i];
        if (args[i] && args[i]->kind == NodeKind::SpreadElement) {
            t = t.kind == TypeKind::Array && !t.parts.empty() ? t.parts.front() : t;
        }
        elem = i == 0 ? widen(t) : unify(elem, widen(t));
    }
    return Type::array(elem);
}

Type Analyzer::inferBufferConstruct(const std::string& cls, const ExprList& args,
                                    const std::vector<Type>& types,
                                    const SourceSpan& span) {
    if (args.size() != 1) {
        report(diagcode::kArityMismatch, span,
               "arity mismatch: expected 1, got " +
                   std::to_string(args.size()) + " arguments");
        return Type::error();
    }
    double lit = 0.0;
    if (args.front() && literalLengthValue(*args.front(), lit) &&
        (lit < 0.0 || std::trunc(lit) != lit)) {
        report(diagcode::kTypeMismatch, span,
               "byte length must be a non-negative integer");
        return Type::error();
    }
    if (!isNumberish(types[0]) && !isPermissive(types[0])) {
        report(diagcode::kTypeMismatch, span,
               "type mismatch: '" + pretty(types[0]) +
                   "' is not a byte length");
        return Type::error();
    }
    return Type::named(TypeKind::Class, cls);
}

Type Analyzer::inferDataViewConstruct(const ExprList& args,
                                      const std::vector<Type>& types,
                                      const SourceSpan& span) {
    if (args.empty() || args.size() > 3) {
        report(diagcode::kArityMismatch, span,
               "arity mismatch: expected 1 to 3, got " +
                   std::to_string(args.size()) + " arguments");
        return Type::error();
    }
    if (!isBufferClass(types[0]) && !isPermissive(types[0])) {
        report(diagcode::kTypeMismatch, span,
               "type mismatch: a DataView needs an ArrayBuffer as its first "
               "argument (got '" + pretty(types[0]) + "')");
        return Type::error();
    }
    for (std::size_t i = 1; i < args.size(); ++i) {
        const SourceSpan aSpan = args[i] ? args[i]->span : span;
        checkAssign(types[i], Type::number(), aSpan, false);
    }
    return Type::named(TypeKind::Class, "DataView");
}

Type Analyzer::inferTypedViewConstruct(const std::string& cls,
                                       const ExprList& args,
                                       const std::vector<Type>& types,
                                       const SourceSpan& span) {
    const Type result = Type::named(TypeKind::Class, cls);
    if (args.empty()) return result;
    if (args.size() == 1) {
        double lit = 0.0;
        if (args.front() && literalLengthValue(*args.front(), lit) &&
            lit < 0.0) {
            report(diagcode::kTypeMismatch, span,
                   "typed-array length must be a non-negative integer");
            return Type::error();
        }
        const Type& t = types[0];
        if (isStringish(t)) {
            report(diagcode::kTypeMismatch, span,
                   "type mismatch: typed-array constructors do not coerce "
                   "scalars ('" +
                       pretty(t) + "' is not a length, an array, a view or a "
                       "buffer)");
            return Type::error();
        }
        if (isNumberish(t) || isPermissive(t) || isTypedArrayClass(t) ||
            t.kind == TypeKind::Array || isBufferClass(t)) {
            return result;
        }
        report(diagcode::kTypeMismatch, span,
               "type mismatch: cannot construct '" + cls + "' from '" +
                   pretty(t) + "' (expected a length, an array, a typed "
                   "array or an ArrayBuffer)");
        return Type::error();
    }
    if (args.size() > 3) {
        report(diagcode::kArityMismatch, span,
               "arity mismatch: expected 1 to 3, got " +
                   std::to_string(args.size()) + " arguments");
        return Type::error();
    }
    if (!isBufferClass(types[0]) && !isPermissive(types[0])) {
        report(diagcode::kTypeMismatch, span,
               "type mismatch: a typed-array view needs an ArrayBuffer as "
               "its first argument (got '" + pretty(types[0]) + "')");
        return Type::error();
    }
    for (std::size_t i = 1; i < args.size(); ++i) {
        const SourceSpan aSpan = args[i] ? args[i]->span : span;
        checkAssign(types[i], Type::number(), aSpan, false);
    }
    return result;
}

// =============================================================================
// Indexed reads + declarator checks
// =============================================================================
Type Analyzer::typedIndexRead(IndexExpr& ix) {
    if (typedIndexProvedInRange(ix)) return Type::number();
    return Type::nullable(Type::number());
}

bool Analyzer::checkArrayCtorDeclarator(const VariableDeclarator& d, bool hasAnn,
                                        const Type& annType,
                                        const Type& initType) {
    if (!d.init || !isArrayLengthForm(*d.init, initType)) return false;
    if (!hasAnn) {
        report(diagcode::kTypeMismatch, d.init->span,
               "cannot infer the element type of Array(n); annotate the "
               "binding or chain .fill(...)");
        return true;
    }
    if (annType.kind != TypeKind::Array) return false;
    const Type elem = elemOf(annType);
    if (isPermissive(elem) || isDefaultInitializable(elem)) return true;
    report(diagcode::kTypeMismatch, d.init->span,
           "Array(n) cannot default-initialize elements of type '" +
               pretty(elem) + "'; chain .fill(...)");
    return true;
}

}  // namespace cp3
