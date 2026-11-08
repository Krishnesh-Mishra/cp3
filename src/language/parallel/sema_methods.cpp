// src/language/parallel/sema_methods.cpp - the typed call forms of `parallel.*`
// (docs/language-spec 13, M2-PLAN item 12).
//
// `parallel.for` / `parallel.reduce` take a closure whose parameter types come
// from the call it drives: the induction variable is `number`, and the
// accumulator matches the initializer's value type (widened). Without the
// hook the closure's arrow is checked against ambient parameters, which makes
// even a trivial `acc.sum` walk a type error, and propagates that ambient
// shape into every fresh object literal the body builds.
// =============================================================================
#include "detail/analyzer.h"
#include "detail/parallel_check.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace {

// The inferred signature of a closure passed to `parallel.*`: the parameter
// types the body was checked against. It is published like `inferArrow`'s so
// codegen lowers the closure with matching types.
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

// The typed call forms: `parallel.for(range, (i) => body)` binds `i` to
// `number`, `parallel.reduce(range, init, (acc, i) => body)` binds `acc` to
// the initializer's value type (widened) and `i` to `number`, then checks the
// body against those parameters. Returns true for any member call this hook
// typed; false when it is not a known `parallel.*` shape.
bool Analyzer::tryInferParallelMemberCall(CallExpr& call, MemberExpr& m,
                                          Type& out) {
    if (!parallel_detail::isParallelRoot(m.object.get())) return false;
    const std::string& prop = m.property;
    if (prop == "for" || prop == "reduce") {
        const std::size_t minArgs = prop == "for" ? 2 : 3;
        if (call.arguments.size() != minArgs || !call.arguments[1]) {
            inferArgs(call);
            out = withType(call, Type::unknown());
            return true;
        }
        Expr* bodyExpr = call.arguments.back().get();
        if (!bodyExpr || bodyExpr->kind != NodeKind::ArrowFunction) {
            inferArgs(call);
            out = withType(call, Type::unknown());
            return true;
        }
        auto& arrow = static_cast<ArrowFunction&>(*bodyExpr);
        std::vector<Type> ctx;
        Type acc = Type::number();
        if (prop == "reduce") {
            acc = widen(inferExpr(*call.arguments[1]));
            ctx = {acc, Type::number()};
        } else {
            ctx = {Type::number()};
        }
        Type ret = widen(inferArrowReturn(arrow, ctx));
        if (arrow.nodeId != 0) {
            FuncSig as = arrowSigOf(arrow, ctx, ret);
            publishFunction(as, "");
            arrowSigs_[arrow.nodeId] = std::move(as);
        }
        if (ret.isUnknown() || ret.isVoid()) ret = acc;
        // Keep `parallel.for` permissive: the `for` part produces nothing,
        // previously the member lookup on the bound name worked regardless.
        out = withType(call, prop == "for" ? ambient() : ret);
        return true;
    } else if (prop == "run") {
        // `parallel.run(f1, ..., fN)` - the body closure takes no parameters,
        // and nothing downstream of the hook (the `$parallelRun` marker is
        // a codegen concern, not sema's) consumes its result type, so keep
        // the previous permissive ambient typing here to avoid tightening
        // member lookups on the result to the ambient-excluded
        // error-on-missing-member rule.
        inferArgs(call);
        out = withType(call, ambient());
        return true;
    }
    return false;
}

}  // namespace cp3
