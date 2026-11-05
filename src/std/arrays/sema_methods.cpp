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

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace {

bool isCallbackMethod(const std::string& prop) {
    return prop == "map" || prop == "filter" || prop == "reduce" ||
           prop == "reduceRight" || prop == "find" || prop == "findIndex" ||
           prop == "findLast" || prop == "findLastIndex" || prop == "some" ||
           prop == "every" || prop == "forEach" || prop == "flatMap";
}

// The JS callback shape: the element, its index, and the receiver. A callback
// may declare any prefix of it; the runtime adapters select the matching call.
std::vector<Type> arrayCallbackContext(const Type& elem) {
    return {elem, Type::number(), Type::array(elem)};
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

    // `flat(depth?)`: the result peels `depth` array layers (1 by default).
    // Only a literal depth is peeled statically; a computed depth yields the
    // innermost type (codegen still requires the literal, so the two agree).
    if (prop == "flat") {
        int depth = 1;
        if (!call.arguments.empty() && call.arguments.front() &&
            call.arguments.front()->kind == NodeKind::NumberLiteral) {
            depth = static_cast<int>(
                static_cast<NumberLiteral&>(*call.arguments.front()).value);
        }
        Type inner = elemOf(recv);
        for (int i = 0; i < depth && inner.kind == TypeKind::Array; ++i) {
            inner = elemOf(inner);
        }
        inferArgs(call);
        out = withType(call, Type::array(inner));
        return true;
    }
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

    const bool isReduce = prop == "reduce" || prop == "reduceRight";
    const std::size_t given = call.arguments.size();
    if (isReduce ? (given < 1 || given > 2) : given != 1) {
        arity(1, isReduce ? 2 : 1);
        inferArgs(call);
        out = withType(call, Type::unknown());
        return true;
    }
    Expr* cb = call.arguments.front().get();
    if (!cb) return false;
    if (cb->kind != NodeKind::ArrowFunction) {
        // A named function (or a function-typed binding) is a callback too:
        // the runtime takes any callable, so the function's own signature
        // types the call. Its parameters may be any prefix of the JS callback
        // shape: `(value)`, `(value, index)`, `(value, index, array)`, and
        // for `reduce` `(accumulator, value, index, array)`.
        const Type fnType = inferExpr(*cb);
        if (fnType.kind != TypeKind::Function || fnType.parts.empty()) {
            return false;
        }
        const Type ret = fnType.parts.back();
        const std::size_t paramCount = fnType.parts.size() - 1;
        const std::size_t minParams = isReduce ? 2 : 0;
        const std::size_t maxParams = isReduce ? 4 : 3;
        if (paramCount < minParams || paramCount > maxParams) {
            arity(minParams, maxParams);
            out = withType(call, Type::unknown());
            return true;
        }
        if (isReduce) {
            Type acc = elem;
            if (given == 2 && call.arguments[1]) {
                acc = widen(inferExpr(*call.arguments[1]));
            }
            checkAssign(acc, fnType.parts[0], call.span, false);
            checkAssign(elem, fnType.parts[1], call.span, false);
            if (paramCount > 2) {
                checkAssign(Type::number(), fnType.parts[2], call.span, false);
            }
            if (paramCount > 3) {
                checkAssign(Type::array(elem), fnType.parts[3], call.span, false);
            }
            Type result = ret;
            if (result.isUnknown() || result.isVoid()) result = acc;
            out = withType(call, result);
            return true;
        }
        checkAssign(elem, fnType.parts[0], call.span, false);
        if (paramCount > 1) {
            checkAssign(Type::number(), fnType.parts[1], call.span, false);
        }
        if (paramCount > 2) {
            checkAssign(Type::array(elem), fnType.parts[2], call.span, false);
        }
        if (prop == "map") {
            out = withType(call, Type::array(ret));
            return true;
        }
        if (prop == "flatMap") {
            if (!ret.isUnknown() && !ret.isVoid() &&
                ret.kind != TypeKind::Array) {
                report(diagcode::kTypeMismatch, call.span,
                       "type mismatch: flatMap() callback must return an "
                       "array, got '" + describe(ret) + "'");
                out = withType(call, Type::unknown());
                return true;
            }
            out = withType(call, ret.isVoid() ? Type::unknown() : ret);
            return true;
        }
        if (prop == "forEach") {
            out = withType(call, Type::voidType());
            return true;
        }
        // The predicate set: the callback must return a boolean; only the
        // result shape differs per method.
        if (!ret.isUnknown() && !ret.isVoid()) {
            checkAssign(ret, Type::boolean(), call.span, false);
        }
        if (prop == "filter") out = withType(call, Type::array(elem));
        else if (prop == "find" || prop == "findLast") {
            out = withType(call, Type::unionOf({elem, Type::undefined()}));
        } else if (prop == "findIndex" || prop == "findLastIndex") {
            out = withType(call, Type::number());
        } else out = withType(call, Type::boolean());
        return true;
    }
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

    std::vector<Type> context = arrayCallbackContext(elem);
    if (prop == "map") {
        Type ret = widen(inferArrowReturn(arrow, context));
        publishArrow(context, ret);
        if (ret.isVoid()) ret = Type::unknown();
        out = withType(call, Type::array(ret));
        return true;
    }

    if (prop == "flatMap") {
        Type ret = widen(inferArrowReturn(arrow, context));
        publishArrow(context, ret);
        if (ret.isUnknown()) {
            out = withType(call, Type::array(Type::unknown()));
            return true;
        }
        if (ret.isVoid() || ret.kind != TypeKind::Array) {
            report(diagcode::kTypeMismatch, arrow.span,
                   "type mismatch: flatMap() callback must return an array, "
                   "got '" + describe(ret) + "'");
            out = withType(call, Type::unknown());
            return true;
        }
        out = withType(call, ret);
        return true;
    }

    if (prop == "forEach") {
        Type ret = widen(inferArrowReturn(arrow, context));
        publishArrow(context, ret);
        out = withType(call, Type::voidType());
        return true;
    }

    // `reduce(fn, initial?)`: the accumulator type is the initial value's;
    // without one JS starts from the first element.
    if (isReduce) {
        Type acc = elem;
        if (given == 2 && call.arguments[1]) {
            acc = widen(inferExpr(*call.arguments[1]));
        }
        std::vector<Type> reduceContext = {acc, elem, Type::number(),
                                           Type::array(elem)};
        Type ret = widen(inferArrowReturn(arrow, reduceContext));
        publishArrow(reduceContext, ret);
        if (ret.isUnknown() || ret.isVoid()) ret = acc;
        out = withType(call, ret);
        return true;
    }

    // The predicate set: the callback receives the element (and may also use
    // its index and the receiver) and must return a boolean; only the result
    // shape differs per method.
    Type ret = widen(inferArrowReturn(arrow, context));
    publishArrow(context, ret);
    if (!ret.isUnknown() && !ret.isVoid()) {
        checkAssign(ret, Type::boolean(), arrow.span, false);
    }
    if (prop == "filter") out = withType(call, Type::array(elem));
    else if (prop == "find" || prop == "findLast") {
        out = withType(call, Type::unionOf({elem, Type::undefined()}));
    } else if (prop == "findIndex" || prop == "findLastIndex") {
        out = withType(call, Type::number());
    } else out = withType(call, Type::boolean());
    return true;
}

}  // namespace cp3
