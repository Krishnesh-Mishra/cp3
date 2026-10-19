// =============================================================================
// src/sema/src/checks/member_check.cpp - member / index expression inference.
//
// Owns property access (`inferMember` / `memberOf` / `memberOfProbe`) and
// subscript access (`inferIndex` / `indexOf` / `indexOfObject` /
// `tupleIndexAt`). Call inference (`inferCall` + argument arity/type
// checking) lives in call_check.cpp; primary expressions live in
// expr_check.cpp (STYLE.md §8).
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// Member access
// =============================================================================
Type Analyzer::inferMember(MemberExpr& m) {
    Type ot = m.object ? inferExpr(*m.object) : Type::unknown();
    // Ambient std namespaces (Math / path / os / process): type the member
    // from the std tables instead of the permissive ambient shape
    // (std_check.cpp). A user binding that shadows the name is not ambient,
    // so it keeps its own shape.
    if (m.object && m.object->kind == NodeKind::Identifier) {
        const auto& id = static_cast<Identifier&>(*m.object);
        Binding b;
        if (lookup(id.name, b) && b.isAmbient) {
            if (auto t = stdNamespaceMember(id.name, m.property)) {
                return withType(m, *t);
            }
            // Closed std families: an unknown member is a real error here,
            // not a codegen CP3002 skip. Open families (process, network,
            // fs, ...) keep the permissive ambient fallthrough.
            if (isClosedStdNamespace(id.name)) {
                reportMemberMiss("'" + id.name + "'", m.property, m.span,
                                 stdNamespaceMemberNames(id.name));
                return withType(m, Type::unknown());
            }
        }
    }
    Type result = memberOf(ot, m.property, m.span, m.optional);
    return withType(m, result);
}

Type Analyzer::memberOf(const Type& ot, const std::string& prop,
                        const SourceSpan& span, bool optional) {
    if (ot.isError()) return Type::error();
    if (isAmbientType(ot)) return ambient();
    if (ot.isUnknown()) {
        report(diagcode::kUnknownType, span,
               "property '" + prop + "' does not exist on type 'unknown'");
        return Type::unknown();
    }
    if (ot.isUnion() || ot.isNullable()) {
        std::vector<Type> results;
        bool sawNullish = optional;
        bool emitted = false;
        for (const Type& m : ot.parts) {
            if (m.isNullish()) {
                sawNullish = true;
                continue;
            }
            Type mr = memberOfProbe(m, prop, span);
            if (mr.isError()) continue;
            if (mr.isUnknown() && !emitted) {
                emitted = true;
            }
            results.push_back(mr);
        }
        Type r = results.empty() ? Type::unknown() : Type::unionOf(results);
        if (sawNullish && !r.isUnknown()) {
            r = unify(r, Type::undefined());
        }
        return r;
    }
    return memberOfProbe(ot, prop, span);
}

// Same as memberOf but for a single (non-union) type; emits CP3305 for a
// genuinely missing property on a fixed-shape object.
Type Analyzer::memberOfProbe(const Type& ot, const std::string& prop,
                             const SourceSpan& span) {
    switch (ot.kind) {
        case TypeKind::Object: {
            if (isAmbientType(ot)) return ambient();
            // The documented std/network marker objects: the marker stays open
            // (an undocumented member falls through to ambient below), but a
            // documented field read carries its declared type; the Map-shaped
            // fields reach codegen as real Map types (src/std/network/sema.cpp).
            if (auto t = networkObjectMember(ot, prop)) return *t;
            Shape* sh = shapeOf(ot);
            if (!sh) return ambient();
            if (sh->open) return ambient();
            if (sh->tuple) {
                return ambient();
            }
            if (const Field* f = findField(sh, prop)) return f->type;
            std::vector<std::string> candidates;
            candidates.reserve(sh->fields.size());
            for (const Field& field : sh->fields) candidates.push_back(field.name);
            reportMemberMiss("object", prop, span, candidates);
            return Type::unknown();
        }
        case TypeKind::Array:
            return arrayMember(ot, prop);
        case TypeKind::Class:
            // Promise members (spec §14): `.then` yields a new promise of the
            // handler's result (unknowable here, so `Promise<unknown>`), while
            // `.catch`/`.catchError`/`.finally` preserve the value type. Codegen
            // re-derives the exact C++ handler types from the receiver.
            if (isPromiseType(ot)) {
                const Type value = promiseValue(ot);
                if (prop == "then") {
                    return Type::function({ambient()}, promiseOf(Type::unknown()));
                }
                if (prop == "catch" || prop == "catchError") {
                    return Type::function({ambient()}, promiseOf(value));
                }
                if (prop == "finally") {
                    return Type::function({ambient()}, promiseOf(value));
                }
                return ambient();
            }
            // `EventEmitter` (D74, spec §14): the documented on/once/off/emit
            // surface, all returning void. A listener is
            // `(...args: unknown[]) => void`; `emit`'s rest parameter is
            // `unknown`, so it accepts any trailing arguments.
            if (isEventEmitterType(ot)) {
                Type listener(TypeKind::Function);
                listener.parts.push_back(Type::unknown());
                listener.parts.push_back(Type::voidType());
                listener.name = kVariadicLabel;
                if (prop == "on" || prop == "once" || prop == "off") {
                    return Type::function({Type::string(), listener},
                                          Type::voidType());
                }
                if (prop == "emit") {
                    // `emit(event, ...args)`: at least `event`, any tail (only
                    // the first value reaches a listener; see emitter.hpp).
                    Type f = Type::function({Type::string(), Type::unknown()},
                                            Type::voidType());
                    f.name = kVariadicOptionalTailLabel;
                    return f;
                }
                reportMemberMiss("EventEmitter", prop, span,
                                 {"on", "once", "off", "emit"});
                return Type::unknown();
            }
            // The JS-compat family (src/std/typed-arrays/sema.cpp): typed
            // views, the buffer classes and DataView have their own tables.
            if (isTypedArrayClass(ot)) return typedArrayMember(ot, prop);
            if (isBufferClass(ot)) return bufferMember(ot, prop);
            if (isDataViewClass(ot)) return dataViewMember(prop);
            // `std/collections` Map/Set (src/std/collections/sema.cpp).
            if (isCollectionsClass(ot)) return collectionsMember(ot, prop, span);
            // `std/date` values: a closed surface (miss -> CP3305 below).
            if (isDateClass(ot)) {
                if (auto t = dateInstanceMember(ot, prop)) return *t;
                reportMemberMiss("Date", prop, span, dateInstanceMemberNames());
                return Type::unknown();
            }
            // The nominal std/buffer `Buffer` (src/std/buffer/sema.cpp).
            if (isByteBufferClass(ot)) return byteBufferMember(ot, prop);
            return ambient();
        case TypeKind::String:
            return stringMember(prop);
        case TypeKind::Number:
        case TypeKind::Boolean:
            return ambient();
        case TypeKind::Literal:
            return memberOfProbe(ot.literalBaseType(), prop, span);
        default:
            return ambient();
    }
}

Type Analyzer::arrayMember(const Type& arr, const std::string& prop) {
    Type elem = elemOf(arr);
    if (prop == "length") return Type::number();
    if (prop == "push" || prop == "unshift") {
        Type f = Type::function({elem}, Type::number());
        f.name = kVariadicLabel;
        return f;
    }
    if (prop == "pop" || prop == "shift") return Type::function({}, elem);
    if (prop == "join") return Type::function({Type::string()}, Type::string());
    if (prop == "sort") {
        // `arr.sort()` / `arr.sort((a, b) => number)`: stable, in place,
        // returns the receiver. The callback is optional; the default ordering
        // is type-aware (docs/language-spec/09-arrays-strings.md).
        Type cmp = Type::function({elem, elem}, Type::number());
        return withOptionalTail(Type::function({cmp}, Type::array(elem)));
    }
    if (prop == "includes") {
        // `includes(searchElement, fromIndex?)` - the literal-set form the
        // emitter can lower today (emit_builtin.cpp).
        return withOptionalTail(
            Type::function({elem, Type::number()}, Type::boolean()));
    }
    return ambient();
}

Type Analyzer::stringMember(const std::string& prop) {
    if (prop == "length") return Type::number();
    if (prop == "charAt" || prop == "at") {
        return Type::function({Type::number()}, Type::string());
    }
    if (prop == "charCodeAt") {
        return Type::function({Type::number()}, Type::number());
    }
    if (prop == "startsWith" || prop == "endsWith" || prop == "includes") {
        return Type::function({Type::string()}, Type::boolean());
    }
    if (prop == "repeat") return Type::function({Type::number()}, Type::string());
    if (prop == "padStart" || prop == "padEnd") {
        return withOptionalTail(
            Type::function({Type::number(), Type::string()}, Type::string()));
    }
    if (prop == "toUpperCase" || prop == "toLowerCase" || prop == "trim" ||
        prop == "trimStart" || prop == "trimEnd") {
        return Type::function({}, Type::string());
    }
    if (prop == "indexOf" || prop == "lastIndexOf") {
        return withOptionalTail(
            Type::function({Type::string(), Type::number()}, Type::number()));
    }
    if (prop == "slice" || prop == "substring") {
        return withOptionalTail(
            Type::function({Type::number(), Type::number()}, Type::string()));
    }
    if (prop == "replace" || prop == "replaceAll") {
        return Type::function({Type::string(), Type::string()}, Type::string());
    }
    return ambient();
}

// =============================================================================
// Subscript access
// =============================================================================
Type Analyzer::inferIndex(IndexExpr& ix) {
    Type ot = ix.object ? inferExpr(*ix.object) : Type::unknown();
    Type result = indexOf(ot, ix);
    return withType(ix, result);
}

Type Analyzer::indexOf(const Type& ot, IndexExpr& ix) {
    Type it = ix.index ? inferExpr(*ix.index) : Type::unknown();
    (void)it;
    if (ot.isError()) return Type::error();
    if (isAmbientType(ot)) return ambient();
    if (ot.isUnknown()) {
        report(diagcode::kUnknownType, ix.span,
               "cannot index into type 'unknown'");
        return Type::unknown();
    }
    if (ot.isUnion() || ot.isNullable()) {
        std::vector<Type> results;
        for (const Type& m : ot.parts) {
            if (m.isNullish()) continue;
            Type r = indexOfProbe(m, ix);
            if (!r.isError()) results.push_back(r);
        }
        return results.empty() ? Type::unknown() : Type::unionOf(results);
    }
    return indexOfProbe(ot, ix);
}

Type Analyzer::indexOfProbe(const Type& ot, IndexExpr& ix) {
    switch (ot.kind) {
        case TypeKind::Array:
            return elemOf(ot);
        case TypeKind::String:
            return Type::number();
        case TypeKind::Object:
            return indexOfObject(ot, ix);
        case TypeKind::Literal:
            return indexOfProbe(ot.literalBaseType(), ix);
        case TypeKind::Class:
            // Typed-array elements are `number` when the literal index is
            // provably in range, `number | undefined` otherwise
            // (src/std/typed-arrays/sema.cpp).
            if (isTypedArrayClass(ot)) return typedIndexRead(ix);
            return ambient();
        default:
            return ambient();
    }
}

Type Analyzer::indexOfObject(const Type& ot, IndexExpr& ix) {
    if (isAmbientType(ot)) return ambient();
    Shape* sh = shapeOf(ot);
    if (sh && sh->open) return ambient();
    if (sh && sh->tuple) return tupleIndexAt(*sh, ix);
    report(diagcode::kIndexSignatureBanned, ix.span,
           "index access on a plain object is not supported; its keys are "
           "fixed at compile time (use Map<K, V> for dynamic keys)");
    return Type::unknown();
}

Type Analyzer::tupleIndexAt(const Shape& sh, IndexExpr& ix) {
    if (ix.index && ix.index->kind == NodeKind::NumberLiteral) {
        auto& n = static_cast<NumberLiteral&>(*ix.index);
        long idx = static_cast<long>(n.value);
        if (idx >= 0 && static_cast<std::size_t>(idx) < sh.tupleElems.size()) {
            return sh.tupleElems[static_cast<std::size_t>(idx)];
        }
    }
    std::vector<Type> es = sh.tupleElems;
    return es.empty() ? Type::unknown() : Type::unionOf(std::move(es));
}

}  // namespace cp3
