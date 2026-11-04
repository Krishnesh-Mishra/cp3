// =============================================================================
// src/sema/src/checks/promise_call.cpp - the Promise statics (spec 14).
//
// The `Promise` surface (spec 14, DECISIONS D40/D42):
//
//   Promise.resolve(v)     -> the promise carries `v`'s type
//   Promise.reject(reason) -> `Promise<void>` (a rejection has no value)
//   Promise.all(iterable)  -> the values, in input order (`Promise<V[]>`)
//   Promise.race(iterable) -> the first promise to settle (`Promise<V>`)
//   Promise.any(iterable)  -> the first promise to fulfil (`Promise<V>`)
//   Promise.withResolvers<T>() -> `{ promise, resolve, reject }` (`$withResolvers`)
//
// The operand of a combinator is an array of promises; a plain value counts as
// an already-resolved promise, exactly like JS, so the result carries the
// promises' value type. Codegen (emit/async_promise.cpp) derives the exact C++
// spelling from these types.
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

Type Analyzer::combinatorOperandValue(CallExpr& call, const std::string& prop) {
    if (call.arguments.size() != 1 || !call.arguments.front()) {
        return Type::unknown();
    }
    const Type operand = typeOfExpr(*call.arguments.front());
    if (operand.kind != TypeKind::Array || operand.parts.empty()) {
        return Type::unknown();
    }
    const Type elem = operand.parts.front();
    // A mixed array of promises can't be lowered without per-element runtime
    // conversion: reject it with a clear diagnostic instead.
    if (elem.kind == TypeKind::Union) {
        report(diagcode::kTypeMismatch, call.span,
               "Promise." + prop +
                   " needs an array of promises with the same value type");
        return Type::unknown();
    }
    return isPromiseType(elem) ? promiseValue(elem) : elem;
}

bool Analyzer::tryInferPromiseStaticCall(CallExpr& call, MemberExpr& m,
                                         Type& out) {
    if (!m.object || m.object->kind != NodeKind::Identifier ||
        static_cast<Identifier&>(*m.object).name != "Promise") {
        return false;
    }
    if (m.property == "allSettled") {
        // The runtime result is an array of settlement records. The record is
        // carried by the `$settled` object marker; codegen spells it
        // `cp3::runtime::Settled<T>` so the binding and the runtime result
        // agree (the array surface then types normally).
        inferArgs(call);
        const Type value = combinatorOperandValue(call, m.property);
        out = withType(call,
                       promiseOf(Type::array(promiseSettledRecordType(value))));
        return true;
    }
    if (m.property == "all" || m.property == "race" || m.property == "any") {
        inferArgs(call);
        const Type value = combinatorOperandValue(call, m.property);
        out = withType(call, promiseOf(m.property == "race" ||
                                             m.property == "any"
                                                 ? value
                                                 : Type::array(value)));
        return true;
    }
    if (m.property == "withResolvers") {
        // The type argument names the value the promise carries; omitting it
        // leaves `unknown` (as in TypeScript's `Promise.withResolvers()`).
        if (call.arguments.size() != 0) {
            report(diagcode::kArityMismatch, call.span,
                   "arity mismatch: Promise.withResolvers takes no arguments");
            out = withType(call, Type::unknown());
            return true;
        }
        Type value = Type::unknown();
        if (!call.typeArgs.empty() && call.typeArgs.front()) {
            value = resolveType(*call.typeArgs.front());
        }
        out = withType(call, promiseWithResolversRecordType(value));
        return true;
    }
    if (m.property != "resolve" && m.property != "reject") return false;
    inferArgs(call);
    if (m.property == "reject") {
        // A rejection carries no observable value; cp3 models it as
        // `Promise<void>` (JS types it `Promise<never>`).
        out = withType(call, promiseOf(Type::voidType()));
        return true;
    }
    // `Promise.resolve(v)`: the promise carries `v`'s type. An existing promise
    // passes through unchanged (JS identity); no argument resolves with
    // `undefined`.
    Type value = Type::undefined();
    Expr* arg = call.arguments.empty() ? nullptr : call.arguments.front().get();
    if (arg) {
        if (arg->kind == NodeKind::SpreadElement) {
            // `Promise.resolve(...values)`: the promise carries the source
            // array's element type - the spread is the literal form of a
            // one-parameter call, so the first element is the value.
            auto& sp = static_cast<SpreadElement&>(*arg);
            Type st = sp.argument ? typeOfExpr(*sp.argument) : Type::unknown();
            Type elem = st.kind == TypeKind::Array && !st.parts.empty()
                            ? st.parts.front() : Type::unknown();
            if (elem.isLiteral()) elem = elem.literalBaseType();
            out = withType(call, promiseOf(elem));
            return true;
        }
        value = typeOfExpr(*arg);
        // TS infers `Promise<number>` (not `Promise<1>`) for
        // `Promise.resolve(1)`: a literal widens in a generic instantiation.
        if (value.isLiteral()) value = value.literalBaseType();
        if (isPromiseType(value)) {
            out = withType(call, value);
            return true;
        }
    }
    out = withType(call, promiseOf(value));
    return true;
}

Type Analyzer::promiseSettledRecordType(const Type& value) {
    const Type reason = Type::named(TypeKind::Class, "Error");
    std::vector<Field> fields{
        Field{"ok", Type::boolean(), false},
        // The runtime struct default-constructs the slot on rejection; the
        // `ok` flag carries the status.
        Field{"value", value, false},
        Field{"reason", reason, false},
    };
    Type st = makeObjectType(std::move(fields), false, false, {});
    const auto label = shapes_.find(st.name);
    if (label != shapes_.end()) {
        Shape shape = label->second;
        st.name = "$settled";
        shapes_[st.name] = std::move(shape);
    }
    st.parts.push_back(value);
    return st;
}

Type Analyzer::promiseWithResolversRecordType(const Type& value) {
    const Type error = Type::named(TypeKind::Class, "Error");
    Type promiseClass = Type::named(TypeKind::Class, "Promise");
    promiseClass.parts.push_back(value);
    const bool isVoid = value.isVoid();
    std::vector<Type> resolveParams;
    if (!isVoid) resolveParams.push_back(value);
    std::vector<Field> fields{
        Field{"promise", promiseClass, false},
        Field{"resolve",
              Type::function(std::move(resolveParams), Type::voidType()),
              false},
        Field{"reject", Type::function({error}, Type::voidType()), false},
    };
    Type st = makeObjectType(std::move(fields), false, false, {});
    const auto label = shapes_.find(st.name);
    if (label != shapes_.end()) {
        Shape shape = label->second;
        st.name = "$withResolvers";
        shapes_[st.name] = std::move(shape);
    }
    st.parts.push_back(value);
    return st;
}

}  // namespace cp3
