// =============================================================================
// src/codegen/src/support/parallel_combine.cpp - the shape gate for the
// chunked `parallel.reduce` lowering.
//
// `parallel.reduce(range, init, body)` keeps the sequential 4-argument runtime
// lowering unless the body is `acc + rhs` with init 0, `acc * rhs` with init 1,
// or the max/min conditional shape, and the per-element expression is proved
// pure and integer exact by parallel_safety.cpp. The runtime schedules chunks
// in an unordered order and combines the per-chunk partials in index order, so
// an impure expression or a floating-point addition order would change the
// observable result.
//
// The proof is shape-directed: the accumulator may appear only as the left
// operand of the fold, and rhs must not mention it at all. That keeps the
// combine text fixed while the body runs independently on every chunk.
// =============================================================================
#include "detail/parallel_combine.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "detail/parallel_safety.h"
#include "detail/type_map.h"

namespace cp3 {
namespace detail {

namespace {

// Strictly below 2^53: the runtime guard uses the same boundary, and an init
// that already sits on it cannot prove the next integer step exact.
constexpr double kExactIntegerLimit = 9007199254740992.0;  // 2^53

bool isNamed(const Expr* e, const std::string& name) {
    if (e == nullptr || e->kind != NodeKind::Identifier) return false;
    return static_cast<const Identifier&>(*e).name == name;
}

// True when `name` occurs free in `e`. Conservative on purpose: a node kind
// this walk does not model counts as a mention, so the combine gate only ever
// accepts a body whose accumulator use it can see whole.
bool mentionsName(const Expr& e, const std::string& name) {
    switch (e.kind) {
        case NodeKind::Identifier:
            return static_cast<const Identifier&>(e).name == name;
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
            return false;
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            return (b.left && mentionsName(*b.left, name)) ||
                   (b.right && mentionsName(*b.right, name));
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(e);
            return (l.left && mentionsName(*l.left, name)) ||
                   (l.right && mentionsName(*l.right, name));
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(e);
            return (c.condition && mentionsName(*c.condition, name)) ||
                   (c.whenTrue && mentionsName(*c.whenTrue, name)) ||
                   (c.whenFalse && mentionsName(*c.whenFalse, name));
        }
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(e);
            return u.operand && mentionsName(*u.operand, name);
        }
        case NodeKind::CallExpr: {
            const auto& c = static_cast<const CallExpr&>(e);
            if (c.callee && mentionsName(*c.callee, name)) return true;
            for (const auto& arg : c.arguments) {
                if (arg && mentionsName(*arg, name)) return true;
            }
            return false;
        }
        default:
            return true;  // not modelled: assume it mentions the accumulator
    }
}

// A reduce parameter must be a plain number parameter. An explicit
// non-number annotation rejects even if sema left the call unchecked: the
// runtime hands the closure a double accumulator and a double index.
bool numberAnnotation(const TypePtr& type) {
    if (!type) return true;
    const std::optional<Type> t = typeFromTypeNode(*type);
    return t && (t->isNumber() ||
                 (t->isLiteral() && t->literalBaseType().isNumber()));
}

bool paramAnnotationIsNumber(const Parameter& p) {
    return numberAnnotation(p.type);
}

// The two plain parameter names of a reduce body; false for anything else
// (rest/ref/optional/default/missing or non-number annotations), which keeps
// the shape out of the combine gate.
bool reduceParamNames(const ArrowFunction& body, std::string& accParam,
                      std::string& indexParam) {
    if (body.params.size() != 2 || !body.params[0] || !body.params[1]) {
        return false;
    }
    for (const auto& param : body.params) {
        if (param->isRest || param->isRef || param->optional ||
            param->defaultValue || !paramAnnotationIsNumber(*param)) {
            return false;
        }
    }
    const Identifier* acc = asIdentifier(body.params[0]->name.get());
    const Identifier* index = asIdentifier(body.params[1]->name.get());
    if (acc == nullptr || index == nullptr) return false;
    accParam = acc->name;
    indexParam = index->name;
    return true;
}

std::string combineOpText(const char* op) {
    return std::string("[](auto cp3_a, auto cp3_b) { return cp3_a ") + op +
           " cp3_b; }";
}

// The max/min conditional shapes; `index` and `acc` are the two operands and
// the condition is the matching comparison. Both max and min are associative
// and idempotent, so they share the runtime's `Max` combine kind (no prefix
// magnitude tracking is needed).
std::optional<ParallelCombine> conditionalCombine(const ConditionalExpr& c,
                                                  const std::string& accParam,
                                                  const std::string& indexParam) {
    if (!c.condition || c.condition->kind != NodeKind::BinaryExpr) {
        return std::nullopt;
    }
    const auto& cmp = static_cast<const BinaryExpr&>(*c.condition);
    const bool indexLeft = isNamed(cmp.left.get(), indexParam);
    const bool accLeft = isNamed(cmp.left.get(), accParam);
    const bool indexRight = isNamed(cmp.right.get(), indexParam);
    const bool accRight = isNamed(cmp.right.get(), accParam);
    if (!isNamed(c.whenTrue.get(), indexParam) ||
        !isNamed(c.whenFalse.get(), accParam)) {
        return std::nullopt;
    }
    if (indexLeft && accRight && cmp.op == BinaryOp::GreaterThan) {
        return ParallelCombine{
            "[](auto cp3_a, auto cp3_b) { return cp3_a > cp3_b ? cp3_a : "
            "cp3_b; }",
            "Max"};
    }
    if (accLeft && indexRight && cmp.op == BinaryOp::LessThan) {
        return ParallelCombine{
            "[](auto cp3_a, auto cp3_b) { return cp3_a > cp3_b ? cp3_a : "
            "cp3_b; }",
            "Max"};
    }
    if (indexLeft && accRight && cmp.op == BinaryOp::LessThan) {
        return ParallelCombine{
            "[](auto cp3_a, auto cp3_b) { return cp3_a < cp3_b ? cp3_a : "
            "cp3_b; }",
            "Max"};
    }
    if (accLeft && indexRight && cmp.op == BinaryOp::GreaterThan) {
        return ParallelCombine{
            "[](auto cp3_a, auto cp3_b) { return cp3_a < cp3_b ? cp3_a : "
            "cp3_b; }",
            "Max"};
    }
    return std::nullopt;
}

}  // namespace

std::optional<ParallelCombine> parallelReduceCombine(
    const ArrowFunction& body, const Expr& init,
    const std::unordered_map<std::string, const FunctionDeclaration*>& functions,
    const SemaResult& sema) {
    if (body.isAsync || !body.typeParams.empty()) return std::nullopt;
    std::string accParam;
    std::string indexParam;
    if (!reduceParamNames(body, accParam, indexParam)) return std::nullopt;
    if (!numberAnnotation(body.returnType)) return std::nullopt;
    if (!body.expressionBody || !body.body) return std::nullopt;
    const Expr* value = dynamic_cast<const Expr*>(body.body.get());
    if (value == nullptr) return std::nullopt;
    const std::optional<std::int64_t> identity = integralLiteralValue(init);
    if (!identity) return std::nullopt;
    if (!(std::fabs(static_cast<double>(*identity)) < kExactIntegerLimit)) {
        return std::nullopt;
    }

    const std::unordered_set<std::string> indexOnly{indexParam};

    if (value->kind == NodeKind::BinaryExpr) {
        const auto& b = static_cast<const BinaryExpr&>(*value);
        if (!b.left || !b.right) return std::nullopt;
        if (!isNamed(b.left.get(), accParam)) return std::nullopt;
        if (mentionsName(*b.right, accParam)) return std::nullopt;
        if (!parallelPureIntegerExpr(*b.right, indexOnly, functions, sema)) {
            return std::nullopt;
        }
        if (b.op == BinaryOp::Add && *identity == 0) {
            return ParallelCombine{combineOpText("+"), "Add"};
        }
        if (b.op == BinaryOp::Multiply && *identity == 1) {
            return ParallelCombine{combineOpText("*"), "Multiply"};
        }
        return std::nullopt;
    }
    if (value->kind == NodeKind::ConditionalExpr) {
        const auto& c = static_cast<const ConditionalExpr&>(*value);
        const std::unordered_set<std::string> both{accParam, indexParam};
        if (!c.condition ||
            !parallelPureIntegerExpr(*c.condition, both, functions, sema)) {
            return std::nullopt;
        }
        if (!c.whenTrue ||
            !parallelPureIntegerExpr(*c.whenTrue, both, functions, sema) ||
            !c.whenFalse ||
            !parallelPureIntegerExpr(*c.whenFalse, both, functions, sema)) {
            return std::nullopt;
        }
        // max/min are associative and idempotent, so any integral literal
        // initializer is safe: every chunk starts from the same init and the
        // ordered combine folds it exactly as the sequential loop would.
        return conditionalCombine(c, accParam, indexParam);
    }
    return std::nullopt;
}

}  // namespace detail
}  // namespace cp3
