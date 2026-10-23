// =============================================================================
// src/sema/src/checks/promise_call.cpp - the Promise statics (spec 14).
//
// The `Promise` surface (spec 14, DECISIONS D40/D42):
//
//   Promise.resolve(v)   -> the promise carries `v`'s type
//   Promise.reject(reason)-> `Promise<void>` (a rejection has no value)
//   Promise.all(iterable) -> the values, in input order (`Promise<V[]>`)
//   Promise.race(iterable)-> the first promise to settle (`Promise<V>`)
//
// The operand of a combinator is an array of promises; a plain value counts as
// an already-resolved promise, exactly like JS, so the result carries the
// promises' value type. Codegen (emit/async_promise.cpp) derives the exact C++
// spelling from these types.
// =============================================================================
#include "detail/analyzer.h"

#include <string>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

bool Analyzer::tryInferPromiseStaticCall(CallExpr& call, MemberExpr& m,
                                         Type& out) {
    if (!m.object || m.object->kind != NodeKind::Identifier ||
        static_cast<Identifier&>(*m.object).name != "Promise") {
        return false;
    }
    if (m.property == "all" || m.property == "race") {
        inferArgs(call);
        Type value = Type::unknown();
        if (call.arguments.size() == 1 && call.arguments.front()) {
            const Type operand = typeOfExpr(*call.arguments.front());
            if (operand.kind == TypeKind::Array && !operand.parts.empty()) {
                const Type elem = operand.parts.front();
                value = isPromiseType(elem) ? promiseValue(elem) : elem;
            }
        }
        out = withType(call, promiseOf(m.property == "race"
                                            ? value
                                            : Type::array(value)));
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
            out = withType(call, promiseOf(elem));
            return true;
        }
        value = typeOfExpr(*arg);
        if (isPromiseType(value)) {
            out = withType(call, value);
            return true;
        }
    }
    out = withType(call, promiseOf(value));
    return true;
}

}  // namespace cp3
