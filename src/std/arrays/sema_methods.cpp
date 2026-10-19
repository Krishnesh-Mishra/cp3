// =============================================================================
// src/std/arrays/sema_methods.cpp - the regular-array method surface's typed
// call forms (docs/language-spec/09-arrays-strings.md).
//
// Most methods get their signature from `arrayMember`
// (src/sema/src/checks/member_check.cpp). The callback methods are inferred
// here instead, with the receiver's element type (and, for `reduce`, the
// initial value's type) as the arrow's parameter context, so the call gets a
// precise result type and the arrow body is checked against real parameters
// rather than ambient ones. The inferred arrow signature is published like
// `inferArrow`'s so codegen lowers the callback with matching types.
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

namespace {

bool isCallbackMethod(const std::string& prop) {
    return prop == "map" || prop == "filter" || prop == "reduce" ||
           prop == "find" || prop == "findIndex" || prop == "some" ||
           prop == "every";
}

// The inferred signature of a callback arrow: the parameter types the body
// was checked against, `unknown` past `ctx`; optional flags as declared.
FuncSig arrowSigOf(const ArrowFunction& arrow, const std::vector<Type>& ctx,
                   const Type& ret) {
    FuncSig as;
    as.ret = ret;
    as.declNodeId = arrow.nodeId;
    for (std::size_t i = 0; i < arrow.params.size(); ++i) {
        const Parameter* p = arrow.params[i].get();
        as.params.push_back(i < ctx.size() ? ctx[i] : Type::unknown());
        as.optional.push_back(p != nullptr &&
                              (p->optional || p->defaultValue != nullptr));
    }
    return as;
}

}  // namespace

bool Analyzer::tryInferArrayCall(CallExpr& call, MemberExpr& m, Type& out) {
    if (!m.object) return false;
    const Type recv = typeOfExpr(*m.object);
    if (recv.kind != TypeKind::Array) return false;
    const std::string& prop = m.property;
    if (!isCallbackMethod(prop)) return false;
    const Type elem = elemOf(recv);

    // The same arity wording `checkArgs` uses (call_check.cpp).
    auto arity = [&](std::size_t required, std::size_t maxArgs) {
        report(diagcode::kArityMismatch, call.span,
               "arity mismatch: expected " +
                   (required == maxArgs
                        ? std::to_string(required)
                        : std::to_string(required) + " to " +
                              std::to_string(maxArgs)) +
                   ", got " + std::to_string(call.arguments.size()) +
                   " argument" + (call.arguments.size() == 1 ? "" : "s"));
    };

    const bool isReduce = prop == "reduce";
    const std::size_t given = call.arguments.size();
    if (isReduce ? (given < 1 || given > 2) : given != 1) {
        arity(1, isReduce ? 2 : 1);
        inferArgs(call);
        out = withType(call, Type::unknown());
        return true;
    }
    Expr* cb = call.arguments.front().get();
    if (!cb || cb->kind != NodeKind::ArrowFunction) return false;
    auto& arrow = static_cast<ArrowFunction&>(*cb);

    // Publish the inferred signature so codegen lowers the arrow with the
    // parameter and return types its body was checked against (the same
    // publication `inferArrow` does for every other arrow, expr_check.cpp).
    auto publishArrow = [&](const std::vector<Type>& ctx, const Type& ret) {
        if (arrow.nodeId == 0) return;
        FuncSig as = arrowSigOf(arrow, ctx, ret);
        publishFunction(as, "");
        arrowSigs_[arrow.nodeId] = std::move(as);
    };

    if (prop == "map") {
        Type ret = widen(inferArrowReturn(arrow, {elem}));
        publishArrow({elem}, ret);
        if (ret.isVoid()) ret = Type::unknown();
        out = withType(call, Type::array(ret));
        return true;
    }

    // `reduce(fn, initial?)`: the accumulator type is the initial value's;
    // without one JS starts from the first element.
    if (isReduce) {
        Type acc = elem;
        if (given == 2 && call.arguments[1]) {
            acc = widen(inferExpr(*call.arguments[1]));
        }
        Type ret = widen(inferArrowReturn(arrow, {acc, elem}));
        publishArrow({acc, elem}, ret);
        if (ret.isUnknown() || ret.isVoid()) ret = acc;
        out = withType(call, ret);
        return true;
    }

    // The predicate set: the callback receives the element and must return a
    // boolean; only the result shape differs per method.
    Type ret = widen(inferArrowReturn(arrow, {elem}));
    publishArrow({elem}, ret);
    if (!ret.isUnknown() && !ret.isVoid()) {
        checkAssign(ret, Type::boolean(), arrow.span, false);
    }
    if (prop == "filter") out = withType(call, Type::array(elem));
    else if (prop == "find") {
        out = withType(call, Type::unionOf({elem, Type::undefined()}));
    } else if (prop == "findIndex") out = withType(call, Type::number());
    else out = withType(call, Type::boolean());
    return true;
}

}  // namespace cp3
