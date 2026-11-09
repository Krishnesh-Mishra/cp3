// =============================================================================
// src/std/object/sema.cpp - the `Object.keys` / `.values` / `.entries` surface,
// plus `Object.assign` / `.is` / `.freeze` family and
// `Object.fromEntries` / `Object.groupBy` (ROADMAP §H + L).
//
// cp3's object literals have a statically known shape (`$o{...}`), so the
// three enumeration helpers can be typed without runtime reflection. The
// result element types follow the shape:
//
//   Object.keys(o)    -> string[]
//   Object.values(o)  -> V[]  (V is the unified field type)
//   Object.entries(o) -> [string, V][]
//
// `Record<string, V>` canonicalizes to `Map<string, V>` (see
// docs/design/record-index-signatures.md), so the two dynamic-key helpers
// return the canonical type directly:
//
//   Object.fromEntries(entries)          -> Map<string, V>   (Record<string, V>)
//   Object.groupBy(list, keyFn)          -> Map<string, T[]> (Record<string, T[]>)
//
// The key side is string-only in both: a non-string entry key / callback
// return is CP3401, exactly the `Record<string, V>` key rule. JS would
// ToPropertyKey-coerce the key; CP3 stays statically typed instead.
//
// Codegen (src/std/object/codegen.cpp) lowers the same shapes onto the
// collections runtime (`makeMap` + `set` loop / `collections::groupBy`); an
// open/computed shape has no static field list and is a type error, not a
// silent `unknown[]`.
// =============================================================================
#include "detail/analyzer.h"

#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace {

bool isObjectBuiltin(const std::string& name) {
    return name == "keys" || name == "values" || name == "entries" ||
           name == "assign" || name == "is" || name == "freeze" ||
           name == "seal" || name == "preventExtensions" ||
           name == "isFrozen" || name == "isSealed" ||
           name == "isExtensible" || name == "getPrototypeOf" ||
           name == "setPrototypeOf" || name == "fromEntries" ||
           name == "groupBy";
}

// The freeze family (`freeze` / `seal` / `preventExtensions`): one argument,
// returned unchanged. CP3 objects have a statically known shape and no
// runtime property table, so immutability is a static-model approximation
// (see codegen.cpp): the call is identity, and the `isFrozen` / `isSealed` /
// `isExtensible` queries fold on a directly frozen argument. The canonical
// runtime registry lives in object.hpp for the wiring step.
bool isFreezeFamily(const std::string& name) {
    return name == "freeze" || name == "seal" ||
           name == "preventExtensions";
}

bool isFreezeQuery(const std::string& name) {
    return name == "isFrozen" || name == "isSealed" ||
           name == "isExtensible";
}

// The canonical `Record<string, V>` type: `TypeKind::Class "Map"` with
// parts {string, V} (docs/design/record-index-signatures.md §2).
Type recordOf(const Type& value) {
    Type t(TypeKind::Class, "Map");
    t.parts = {Type::string(), value};
    return t;
}

// Extract the [key, value] pair from an entry element type: a `$mapEntry`
// tuple (Map.entries), a `$t[...]` tuple (Object.entries, `[string, V][]`,
// a `[k, v]` literal after contextual typing) - both carry parts {K, V}.
bool entryPairOf(const Type& elem, Type& key, Type& value) {
    if (elem.kind != TypeKind::Object || elem.parts.size() < 2) return false;
    if (elem.name != "$mapEntry" && !isTupleType(elem)) return false;
    key = elem.parts.front();
    value = elem.parts[1];
    return true;
}

bool stringKeyOf(const Type& key) {
    return isStringish(key) || isPermissive(key) || key.isUnknown();
}

}  // namespace

bool Analyzer::tryInferObjectCall(CallExpr& call, MemberExpr& m, Type& out) {
    if (!m.object || m.object->kind != NodeKind::Identifier ||
        static_cast<Identifier&>(*m.object).name != "Object") {
        return false;
    }
    if (!isObjectBuiltin(m.property)) return false;
    Binding objectBinding;
    if (lookup("Object", objectBinding) && !objectBinding.isAmbient) {
        return false;  // a user binding shadows the global
    }
    inferArgs(call);
    if (m.property == "is") {
        if (call.arguments.size() != 2 || !call.arguments.front() ||
            !call.arguments[1]) {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: Object.is expects two arguments");
            out = withType(call, Type::unknown());
            return true;
        }
        out = withType(call, Type::boolean());
        return true;
    }
    if (isFreezeFamily(m.property)) {
        if (call.arguments.size() != 1 || !call.arguments.front()) {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: Object." + m.property +
                       " expects one argument");
            out = withType(call, Type::unknown());
            return true;
        }
        const Type recv = typeOfExpr(*call.arguments.front());
        if (recv.isUnknown()) {
            report(diagcode::kTypeMismatch, call.span,
                   "Object." + m.property +
                       " requires a value with a known type");
            out = withType(call, Type::unknown());
            return true;
        }
        // JS: freeze/seal/preventExtensions on a primitive returns the
        // primitive unchanged; on an object it returns the same object.
        // CP3 follows the same identity contract (codegen.cpp).
        out = withType(call, recv);
        return true;
    }
    if (isFreezeQuery(m.property)) {
        if (call.arguments.size() != 1 || !call.arguments.front()) {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: Object." + m.property +
                       " expects one argument");
            out = withType(call, Type::unknown());
            return true;
        }
        const Type recv = typeOfExpr(*call.arguments.front());
        if (recv.isUnknown()) {
            report(diagcode::kTypeMismatch, call.span,
                   "Object." + m.property +
                       " requires a value with a known type");
            out = withType(call, Type::unknown());
            return true;
        }
        out = withType(call, Type::boolean());
        return true;
    }
    if (m.property == "getPrototypeOf") {
        if (call.arguments.size() != 1 || !call.arguments.front()) {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: Object.getPrototypeOf expects one "
                   "argument");
            out = withType(call, Type::unknown());
            return true;
        }
        const Type recv = typeOfExpr(*call.arguments.front());
        if (recv.isUnknown()) {
            report(diagcode::kTypeMismatch, call.span,
                   "Object.getPrototypeOf requires a value with a known "
                   "type");
            out = withType(call, Type::unknown());
            return true;
        }
        // CP3 object literals are plain static shapes with no prototype
        // chain and no runtime metadata, so the prototype is always null.
        out = withType(call, Type::nullType());
        return true;
    }
    if (m.property == "setPrototypeOf") {
        if (call.arguments.size() != 2 || !call.arguments.front() ||
            !call.arguments[1]) {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: Object.setPrototypeOf expects two "
                   "arguments");
            out = withType(call, Type::unknown());
            return true;
        }
        const Type recv = typeOfExpr(*call.arguments.front());
        if (recv.isUnknown()) {
            report(diagcode::kTypeMismatch, call.span,
                   "Object.setPrototypeOf requires a value with a known "
                   "type");
            out = withType(call, Type::unknown());
            return true;
        }
        const Type proto = typeOfExpr(*call.arguments[1]);
        if (!proto.isNull()) {
            // ROADMAP §7: TypeScript allows a runtime prototype, but CP3
            // does not, because CP3 emits static native output with no
            // runtime metadata and no dynamic class resolution. A null
            // prototype is the only setting that matches the static model.
            report(diagcode::kTypeMismatch, call.arguments[1]->span,
                   "type mismatch: Object.setPrototypeOf's prototype must "
                   "be null (CP3 objects have no prototype; TypeScript "
                   "allows a runtime prototype, but CP3 does not because "
                   "CP3 emits static native output with no runtime "
                   "metadata or dynamic class resolution)");
            out = withType(call, recv);
            return true;
        }
        out = withType(call, recv);
        return true;
    }
    if (m.property == "assign") {
        if (call.arguments.size() < 2 || !call.arguments.front()) {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: Object.assign expects at least two "
                   "arguments");
            out = withType(call, Type::unknown());
            return true;
        }
        // The first argument is the target that is mutated and returned. The
        // remaining sources have to be objects with statically known fields
        // (extra properties are ignored; the target's shape is fixed).
        const Type target = typeOfExpr(*call.arguments.front());
        if (target.isUnknown()) {
            report(diagcode::kTypeMismatch, call.span,
                   "Object.assign target must have a known type");
            out = withType(call, Type::unknown());
            return true;
        }
        if (target.kind != TypeKind::Object) {
            report(diagcode::kTypeMismatch, call.span,
                   "Object.assign target must be an object");
            out = withType(call, target);
            return true;
        }
        for (std::size_t i = 1; i < call.arguments.size(); ++i) {
            const ExprPtr& arg = call.arguments[i];
            if (!arg) continue;
            const Type source = typeOfExpr(*arg);
            // JS: null and undefined sources are ignored by Object.assign.
            if (source.isNullish()) continue;
            // A nullable object source is allowed; codegen null-checks it
            // before copying (a null value at runtime is ignored, as in JS).
            Type sourceType = source;
            if (source.isNullable() && !source.parts.empty()) {
                sourceType = source.parts.front();
                if (sourceType.isNullish()) continue;
            }
            const Shape* sourceShape = shapeOf(sourceType);
            if (sourceType.kind != TypeKind::Object || sourceShape == nullptr ||
                sourceShape->open || sourceShape->tuple) {
                report(diagcode::kTypeMismatch, arg->span,
                       "Object.assign sources must be objects with statically "
                       "known fields (or null/undefined, which are ignored)");
            }
        }
        const Expr* frontArg = call.arguments.front().get();
        if (frontArg && frontArg->kind == NodeKind::ObjectLiteral) {
            // A fresh literal target: the call merges its fields with every
            // source's (JS key order, later source wins a duplicate key,
            // the new object is fresh). The merged shape is the result type.
            std::vector<Field> merged;
            const Shape* targetShape = shapeOf(target);
            if (targetShape != nullptr) {
                merged = targetShape->fields;
            }
            for (std::size_t i = 1; i < call.arguments.size(); ++i) {
                const ExprPtr& arg = call.arguments[i];
                if (!arg) continue;
                const Type source = typeOfExpr(*arg);
                if (source.isNullish()) continue;
                Type sourceType = source;
                if (source.isNullable() && !source.parts.empty()) {
                    sourceType = source.parts.front();
                    if (sourceType.isNullish()) continue;
                } else if (source.isNullable()) {
                    continue;
                }
                const Shape* sourceShape = shapeOf(sourceType);
                if (sourceType.kind != TypeKind::Object || sourceShape == nullptr) {
                    continue;  // already reported above; keep going for the type
                }
                for (const Field& f : sourceShape->fields) {
                    bool replaced = false;
                    for (Field& existing : merged) {
                        if (existing.name == f.name) {
                            existing.type = f.type;
                            existing.optional = f.optional;
                            replaced = true;
                            break;
                        }
                    }
                    if (!replaced) {
                        merged.push_back(f);
                    }
                }
            }
            Type mergedType =
                makeObjectType(std::move(merged), false, false, {});
            out = withType(call, mergedType);
            return true;
        }
        const Shape* targetShape = shapeOf(target);
        if (targetShape != nullptr) {
            // A reused binding has a fixed declared shape; a source with a
            // field the target does not declare would change its shape. The
            // merge is only allowed for a fresh literal (above). TypeScript
            // permits extending an existing object's declared shape with
            // Object.assign; cp3 does not — say so and point at the spread
            // form that the language does support.
            for (std::size_t i = 1; i < call.arguments.size(); ++i) {
                const ExprPtr& arg = call.arguments[i];
                if (!arg) continue;
                const Type source = typeOfExpr(*arg);
                if (source.isNullish()) continue;
                Type sourceType = source;
                if (source.isNullable() && !source.parts.empty()) {
                    sourceType = source.parts.front();
                    if (sourceType.isNullish()) continue;
                }
                const Shape* sourceShape = shapeOf(sourceType);
                if (sourceShape == nullptr) continue;
                for (const Field& f : sourceShape->fields) {
                    bool present = false;
                    for (const Field& tf : targetShape->fields) {
                        if (tf.name == f.name) { present = true; break; }
                    }
                    if (!present) {
                        report(diagcode::kTypeMismatch, arg->span,
                               "Object.assign cannot add a new property to an "
                               "existing object in cp3; TypeScript allows it, "
                               "but CP3 emits statically typed native output "
                               "with fixed object shapes — write the spread "
                               "form `{...t, ...u}` instead");
                        break;
                    }
                }
            }
        }
        out = withType(call, target);
        return true;
    }
    if (m.property == "fromEntries") {
        if (call.arguments.size() != 1 || !call.arguments.front()) {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: Object.fromEntries expects exactly one "
                   "argument (the entries array), got " +
                       std::to_string(call.arguments.size()));
            out = withType(call, Type::unknown());
            return true;
        }
        Expr* arg = call.arguments.front().get();
        Type value = Type::unknown();
        bool haveValue = false;
        bool bad = false;
        if (arg->kind == NodeKind::ArrayLiteral) {
            auto& lit = static_cast<ArrayLiteral&>(*arg);
            if (lit.elements.empty()) {
                report(diagcode::kTypeMismatch, arg->span,
                       "type mismatch: Object.fromEntries cannot infer the "
                       "value type from an empty entries array; use a typed "
                       "entries variable or Record<string, V>");
                out = withType(call, Type::unknown());
                return true;
            }
            for (auto& e : lit.elements) {
                if (!e) continue;
                Type key = Type::unknown();
                Type v = Type::unknown();
                bool pair = false;
                if (e->kind == NodeKind::ArrayLiteral) {
                    auto& inner = static_cast<ArrayLiteral&>(*e);
                    if (inner.elements.size() != 2 || !inner.elements.front() ||
                        !inner.elements[1]) {
                        report(diagcode::kTypeMismatch, e->span,
                               "type mismatch: each Object.fromEntries entry "
                               "must be a [key, value] pair");
                        bad = true;
                        continue;
                    }
                    key = inferExpr(*inner.elements.front());
                    v = inferExpr(*inner.elements[1]);
                    pair = true;
                } else {
                    pair = entryPairOf(inferExpr(*e), key, v);
                    if (!pair) {
                        report(diagcode::kTypeMismatch, e->span,
                               "type mismatch: each Object.fromEntries entry "
                               "must be a [key, value] pair");
                        bad = true;
                        continue;
                    }
                }
                if (pair && !stringKeyOf(key)) {
                    report(diagcode::kTypeMismatch, e->span,
                           "type mismatch: Object.fromEntries keys must be "
                           "strings (Record<string, V> requires a string key)");
                    bad = true;
                }
                const Type widened = widen(v);
                if (!haveValue) {
                    value = widened;
                    haveValue = true;
                } else {
                    value = unify(value, widened);
                }
            }
            if (bad || !haveValue) {
                out = withType(call, Type::unknown());
                return true;
            }
            // Contextually re-check the literal against the inferred entry
            // type so codegen gets the tuple spelling (mirrors the
            // `new Map<K, V>(entries)` from-iterable path in expr_check.cpp).
            Type entry = makeObjectType({}, false, true,
                                        {Type::string(), value});
            checkArrayLiteral(lit, Type::array(entry));
        } else {
            const Type entries = inferExpr(*arg);
            if (entries.kind != TypeKind::Array) {
                report(diagcode::kTypeMismatch, arg->span,
                       "type mismatch: Object.fromEntries expects an array "
                       "of [key, value] entries");
                out = withType(call, Type::unknown());
                return true;
            }
            Type key = Type::unknown();
            if (!entryPairOf(elemOf(entries), key, value)) {
                report(diagcode::kTypeMismatch, arg->span,
                       "type mismatch: Object.fromEntries expects an array "
                       "of [key, value] entries, e.g. Object.entries(o) or "
                       "[[\"a\", 1]]");
                out = withType(call, Type::unknown());
                return true;
            }
            if (!stringKeyOf(key)) {
                report(diagcode::kTypeMismatch, arg->span,
                       "type mismatch: Object.fromEntries keys must be "
                       "strings (Record<string, V> requires a string key)");
                out = withType(call, Type::unknown());
                return true;
            }
        }
        out = withType(call, recordOf(value));
        return true;
    }
    if (m.property == "groupBy") {
        if (call.arguments.size() != 2) {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: Object.groupBy expects exactly 2 "
                   "arguments (the list and the key callback), got " +
                       std::to_string(call.arguments.size()));
            inferArgs(call);
            out = withType(call, Type::unknown());
            return true;
        }
        Type items = call.arguments.front() ? inferExpr(*call.arguments.front())
                                            : Type::unknown();
        if (items.kind != TypeKind::Array) {
            report(diagcode::kTypeMismatch,
                   call.arguments.front() ? call.arguments.front()->span
                                          : call.span,
                   "type mismatch: Object.groupBy's first argument must be an "
                   "array");
            inferArgs(call);
            out = withType(call, Type::unknown());
            return true;
        }
        const Type elem = elemOf(items);
        Type key = Type::unknown();
        Expr* cb = call.arguments[1] ? call.arguments[1].get() : nullptr;
        if (cb != nullptr && cb->kind == NodeKind::ArrowFunction) {
            auto& arrow = static_cast<ArrowFunction&>(*cb);
            const std::vector<Type> context = {elem, Type::number(),
                                               Type::array(elem)};
            key = widen(inferArrowReturn(arrow, context));
            // Publish the inferred signature for codegen's parameter types
            // (mirrors Map.groupBy in src/std/collections/sema.cpp).
            if (arrow.nodeId != 0) {
                FuncSig as;
                as.ret = key;
                as.declNodeId = arrow.nodeId;
                for (std::size_t i = 0; i < arrow.params.size(); ++i) {
                    const Parameter* p = arrow.params[i].get();
                    as.params.push_back(i < context.size() ? context[i]
                                                           : Type::unknown());
                    as.optional.push_back(p != nullptr &&
                                          (p->optional ||
                                           p->defaultValue != nullptr));
                }
                publishFunction(as, "");
                arrowSigs_[arrow.nodeId] = std::move(as);
            }
        } else if (cb != nullptr) {
            const Type fnType = inferExpr(*cb);
            if (fnType.kind == TypeKind::Function && !fnType.parts.empty()) {
                key = widen(fnType.parts.back());
            }
        }
        // Do NOT call inferArgs(call) here for the arrow form: it would
        // re-infer the callback with ambient parameters and overwrite the
        // signature published above (see Map.groupBy).
        if (cb != nullptr && cb->kind == NodeKind::ArrowFunction) {
            const std::vector<Type> context = {elem, Type::number(),
                                               Type::array(elem)};
            withType(*cb, Type::function(context, key));
        }
        if (key.isVoid()) {
            report(diagcode::kTypeMismatch,
                   call.arguments[1] ? call.arguments[1]->span : call.span,
                   "type mismatch: Object.groupBy callback must return a "
                   "string key, got 'void'");
            out = withType(call, Type::unknown());
            return true;
        }
        if (!stringKeyOf(key)) {
            report(diagcode::kTypeMismatch,
                   call.arguments[1] ? call.arguments[1]->span : call.span,
                   "type mismatch: Object.groupBy callback must return a "
                   "string key (Record<string, T[]> requires a string key); "
                   "use Map.groupBy for non-string keys");
            out = withType(call, Type::unknown());
            return true;
        }
        out = withType(call, recordOf(Type::array(elem)));
        return true;
    }
    if (call.arguments.size() != 1 || !call.arguments.front()) {
        report(diagcode::kArityMismatch, call.span,
               "arity mismatch: Object." + m.property +
                   " expects one object argument");
        out = withType(call, Type::unknown());
        return true;
    }
    const Type object = typeOfExpr(*call.arguments.front());
    const Shape* shape = shapeOf(object);
    if (shape == nullptr || shape->open || shape->tuple ||
        shape->fields.empty()) {
        report(diagcode::kTypeMismatch, call.span,
               "type mismatch: Object." + m.property +
                   " requires an object with statically known fields");
        out = withType(call, Type::unknown());
        return true;
    }
    if (m.property == "keys") {
        out = withType(call, Type::array(Type::string()));
        return true;
    }
    Type value = shape->fields.front().type;
    if (shape->fields.front().optional) {
        value = unify(value, Type::undefined());
    }
    for (std::size_t i = 1; i < shape->fields.size(); ++i) {
        Type next = shape->fields[i].type;
        if (shape->fields[i].optional) next = unify(next, Type::undefined());
        value = unify(value, next);
    }
    if (m.property == "values") {
        out = withType(call, Type::array(value));
        return true;
    }
    Type entry = makeObjectType({}, false, true,
                                {Type::string(), value});
    out = withType(call, Type::array(entry));
    return true;
}

// `Error.captureStackTrace(error, fn?)`: refreshes the error's `stack` from
// its current representation (cp3 captures no true JS stack in v0.2: `stack`
// is `name: message` at construction; the captured text gets one `at <fn>`
// frame when the optional fn is given). Result: `void`.
bool Analyzer::tryInferErrorStaticCall(CallExpr& call, MemberExpr& m,
                                       Type& out) {
    if (!m.object || m.object->kind != NodeKind::Identifier ||
        static_cast<Identifier&>(*m.object).name != "Error" ||
        m.property != "captureStackTrace") {
        return false;
    }
    Binding errorBinding;
    if (lookup("Error", errorBinding) && !errorBinding.isAmbient) {
        return false;  // a user `Error` shadows the builtin
    }
    if (call.arguments.empty() || !call.arguments.front()) {
        report(diagcode::kArityMismatch, call.span,
               "arity mismatch: Error.captureStackTrace expects an error "
               "argument");
        inferArgs(call);
        out = withType(call, Type::unknown());
        return true;
    }
    Type target = inferExpr(*call.arguments.front());
    if (!isErrorClass(target)) {
        report(diagcode::kTypeMismatch, call.span,
               "type mismatch: Error.captureStackTrace's first argument must "
               "be an Error");
        inferArgs(call);
        out = withType(call, Type::unknown());
        return true;
    }
    inferArgs(call);
    out = withType(call, Type::voidType());
    return true;
}

}  // namespace cp3
