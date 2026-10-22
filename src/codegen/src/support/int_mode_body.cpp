// =============================================================================
// src/codegen/src/support/int_mode_body.cpp - the int64 fast path's helpers.
//
// The divisibility facts (a branch that proved `name % k == 0`), the effect
// predicate (a node the fast body must not lower, because the wrapper re-runs
// the double body after a bail) and the operators that need a shape of their
// own (the product, the update, the deferred raw `+`/`-`) live here. The state,
// the predicates and the orchestration live in support/int_mode.cpp.
// =============================================================================
#include "detail/generator.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "detail/int_div_facts.h"
#include "detail/int_reads.h"
#include "detail/structural_walk.h"

namespace cp3 {

using namespace detail;

namespace {

constexpr const char* kArith = "::cp3::runtime::arith::";

// A placeholder for a failed attempt: the text is thrown away with it.
constexpr const char* kNoValue = "std::int64_t(0)";

std::string arith(const char* fn) { return std::string(kArith) + fn; }

// --- fact stability ----------------------------------------------------------
//
// The fact `name % k == 0` is a compile-time claim about the value of `name`
// at every later point in the then-branch. It is sound only when the branch
// runs its statements once, in source order, and nothing between the guard and
// the division can change `name` except a syntactic write (which the fact
// invalidation sees at the right point). Anything that can defer, repeat, or
// outsource execution breaks that:
//   * loops re-run already-emitted code after a write;
//   * functions, arrows and classes defer bodies to a later call;
//   * calls, construction and await can run such a body;
//   * member and index access can run an accessor declared elsewhere.
// Reject the whole branch when any of those occurs. A lost fact only costs one
// remainder check; a missed mutation is a miscompile.
class FactStabilityScan final : public StructuralWalk {
public:
    bool stable(const Stmt& branch) {
        walk(branch);
        return !found_;
    }

protected:
    bool onNode(const Node& n) override {
        switch (n.kind) {
            case NodeKind::ArrowFunction:
            case NodeKind::FunctionDeclaration:
            case NodeKind::ClassDeclaration:
            case NodeKind::CallExpr:
            case NodeKind::NewExpr:
            case NodeKind::AwaitExpr:
            case NodeKind::MemberExpr:
            case NodeKind::IndexExpr:
            case NodeKind::WhileStatement:
            case NodeKind::DoWhileStatement:
            case NodeKind::ForStatement:
            case NodeKind::ForOfStatement:
            case NodeKind::ForInStatement:
            case NodeKind::TryStatement:
            case NodeKind::ThrowStatement:
                found_ = true;
                return true;
            default:
                return false;
        }
    }

private:
    bool found_ = false;
};

}  // namespace

namespace detail {

void dropDivisibleFacts(IntMode* mode, const std::string& name) {
    if (mode == nullptr) return;
    for (IntMode::DivisibleFact& fact : mode->divisible) {
        if (fact.name == name) fact.valid = false;
    }
}

std::optional<std::int64_t> divisibleModulus(const BinaryExpr& b,
                                             const IntMode* mode) {
    if (mode == nullptr || !b.left || b.left->kind != NodeKind::Identifier ||
        !b.right) {
        return std::nullopt;
    }
    const auto k = integralLiteralValue(*b.right);
    if (!k || *k <= 0) return std::nullopt;
    const std::string& name = static_cast<const Identifier&>(*b.left).name;
    for (const auto& fact : mode->divisible) {
        if (fact.valid && fact.name == name && fact.mod == *k) return *k;
    }
    return std::nullopt;
}

bool branchIsFactStable(const Stmt& branch) {
    FactStabilityScan scan;
    return scan.stable(branch);
}

}  // namespace detail

bool Generator::factStableBranch(const Stmt& branch) const {
    return detail::branchIsFactStable(branch);
}

// True when lowering `e` inside the fast body would run an observable side
// effect (or bind a `ref` argument to an int64 local). The wrapper re-runs the
// double body after a bail, so any such node would happen twice.
bool Generator::intEffectful(const Expr& e) const {
    if (e.kind == NodeKind::CallExpr || e.kind == NodeKind::NewExpr ||
        e.kind == NodeKind::AwaitExpr) {
        return true;
    }
    // A write is allowed only as the whole expression of a statement. Nested
    // writes would be evaluated in C++'s unspecified operand order (or after
    // the other operand), while JS evaluates left to right; reject the attempt
    // rather than guessing.
    if ((e.kind == NodeKind::AssignmentExpr ||
         e.kind == NodeKind::UpdateExpr) &&
        !(intMode_ && intMode_->stmtExpr)) {
        return true;
    }
    if (e.kind == NodeKind::AssignmentExpr) {
        const auto& a = static_cast<const AssignmentExpr&>(e);
        if (!a.target || a.target->kind != NodeKind::Identifier) return true;
        // A module global or a shared cell is not a frame-local the bail
        // discards: its store would be observable before the double re-run.
        return !intLocal(static_cast<const Identifier&>(*a.target).name);
    }
    if (e.kind == NodeKind::UpdateExpr) {
        const auto& u = static_cast<const UpdateExpr&>(e);
        if (!u.operand || u.operand->kind != NodeKind::Identifier) return true;
        return !intLocal(static_cast<const Identifier&>(*u.operand).name);
    }
    if (e.kind == NodeKind::UnaryExpr) {
        return static_cast<const UnaryExpr&>(e).op == UnaryOp::Delete;
    }
    return false;
}

// The declaration text of a proven entry-body int64 binding: its int64
// spelling, initialized with the proven start value. Empty when this
// declarator is not such a binding.
std::string Generator::int64DeclaratorText(const VariableDeclarator& d) {
    if (!d.id || d.id->kind != NodeKind::Identifier) return std::string();
    const auto it = topInt64Bindings_.find(
        static_cast<const Identifier&>(*d.id).name);
    if (it == topInt64Bindings_.end() || it->second.declNode != d.nodeId) {
        return std::string();
    }
    return "std::int64_t " + it->second.alias + " = " +
           std::to_string(it->second.start);
}

// The guard every expression of the fast body passes: a call, a construction,
// an await, a delete or a write outside a statement position is an observable
// side effect the fast body must not run (the wrapper re-runs the double body
// after a bail, so it would happen twice). A nested write would also follow
// C++'s unspecified operand order, while JS evaluates left to right.
void Generator::intGuardExpr(const Expr& e) {
    if (intEffectful(e)) intFail();
    // Only a statement's whole expression may be a write (intEffectful): clear
    // the flag before descending into the operands of any other expression so
    // a nested write rejects the attempt.
    if (intMode_ && intMode_->stmtExpr &&
        e.kind != NodeKind::AssignmentExpr &&
        e.kind != NodeKind::UpdateExpr) {
        intMode_->stmtExpr = false;
    }
}

// The read text of an entry-body int64 binding: the register converted back to
// the double `number` the source sees. |value| <= 2^53 under the proof, so the
// conversion is exact. Empty when the name is not such a binding.
std::string Generator::int64ReadText(const std::string& name) {
    const auto it = topInt64Bindings_.find(name);
    if (it == topInt64Bindings_.end()) return std::string();
    return "static_cast<double>(" + it->second.alias + ")";
}

std::optional<std::pair<std::string, std::int64_t>> Generator::divisibleGuard(
    const Expr& e) {
    if (!intMode_ || e.kind != NodeKind::BinaryExpr) return std::nullopt;
    const auto& b = static_cast<const BinaryExpr&>(e);
    if (b.op != BinaryOp::Equal && b.op != BinaryOp::StrictEqual) {
        return std::nullopt;
    }
    if (!b.left || !b.right) return std::nullopt;
    const Expr* mod = nullptr;
    const Expr* zero = nullptr;
    if (b.left->kind == NodeKind::BinaryExpr) {
        mod = b.left.get();
        zero = b.right.get();
    } else if (b.right->kind == NodeKind::BinaryExpr) {
        mod = b.right.get();
        zero = b.left.get();
    } else {
        return std::nullopt;
    }
    if (zero->kind != NodeKind::NumberLiteral ||
        static_cast<const NumberLiteral&>(*zero).value != 0.0) {
        return std::nullopt;
    }
    const auto& mb = static_cast<const BinaryExpr&>(*mod);
    if (mb.op != BinaryOp::Modulo || !mb.left || !mb.right ||
        mb.left->kind != NodeKind::Identifier) {
        return std::nullopt;
    }
    const auto k = integralLiteralValue(*mb.right);
    const auto& id = static_cast<const Identifier&>(*mb.left);
    if (!k || *k <= 0 || !numberTyped(id)) return std::nullopt;
    // A deferred binding may sit outside the exact int64 range; idiv would
    // have bailed on it, so the guard would not license a raw division.
    if (intMode_->deferred.count(id.name) != 0) return std::nullopt;
    return std::make_pair(id.name, *k);
}

std::string Generator::intProduct(const BinaryExpr& b) {
    const std::string lhs = expr(*b.left);
    const std::string rhs = expr(*b.right);
    if (auto k = integralLiteralValue(*b.left); k && *k != 0) {
        return arith("imulConst<") + std::to_string(*k) + "LL>(" + rhs + ")";
    }
    if (auto k = integralLiteralValue(*b.right); k && *k != 0) {
        return arith("imulConst<") + std::to_string(*k) + "LL>(" + lhs + ")";
    }
    return arith("imul(") + lhs + ", " + rhs + ")";
}

std::string Generator::intUpdate(const UpdateExpr& u) {
    if (!u.operand || u.operand->kind != NodeKind::Identifier ||
        !numberTyped(*u.operand)) {
        intFail();
        return kNoValue;
    }
    const auto& id = static_cast<const Identifier&>(*u.operand);
    // A module global or a shared cell is not an int64 local.
    if (!intLocal(id.name)) {
        intFail();
        return kNoValue;
    }
    // The helpers take a reference, so the old value must be in range *before*
    // the update, and a deferred read is a comma expression, not an lvalue.
    if (intMode_ && intMode_->deferred.count(id.name)) {
        intFail();
        return kNoValue;
    }
    const std::string target = expr(*u.operand);
    detail::dropDivisibleFacts(intMode_.get(), id.name);
    const bool inc = u.op == UpdateOp::Increment;
    const char* fn = u.prefix ? (inc ? "preInc" : "preDec") : (inc ? "postInc" : "postDec");
    return "(" + arith(fn) + "(" + target + "))";
}

std::string Generator::intAssignValue(const Expr& e) {
    if (e.kind != NodeKind::BinaryExpr) return std::string();
    const auto& b = static_cast<const BinaryExpr&>(e);
    if (b.op != BinaryOp::Add && b.op != BinaryOp::Subtract) return std::string();
    if (!b.left || !b.right) return std::string();
    bool kOnLeft = false;
    std::optional<std::int64_t> k;
    if (auto l = integralLiteralValue(*b.left); l && *l > 0 && *l < 513) {
        k = l;
        kOnLeft = true;
    } else if (auto r = integralLiteralValue(*b.right); r && *r > 0 && *r < 513) {
        k = r;
    }
    if (!k) return std::string();
    const std::string lit = "std::int64_t(" + std::to_string(*k) + ")";
    const std::string lhs = kOnLeft ? lit : intRawOperand(*b.left);
    const std::string rhs = kOnLeft ? intRawOperand(*b.right) : lit;
    return "(" + lhs + (b.op == BinaryOp::Add ? " + " : " - ") + rhs + ")";
}

// An operand of a raw `+`/`-`. A deferred value feeds it without a read
// re-check: the operation is exact at any magnitude and the result is stored
// back into a deferred variable, so the value is still not observed. Every
// other operand is lowered normally, which re-checks a deferred read.
std::string Generator::intRawOperand(const Expr& e) {
    if (e.kind == NodeKind::Identifier) {
        const auto& id = static_cast<const Identifier&>(e);
        if (intMode_ && intMode_->deferred.count(id.name)) return mapIdent(id.name);
    }
    return expr(e);
}

}  // namespace cp3
