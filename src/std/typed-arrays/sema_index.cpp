// =============================================================================
// src/std/typed-arrays/sema_index.cpp - in-range proof for typed-array indexing.
//
// Implements D69 (typed-array indexing) plus the typed-array index proof (D72):
//   * `constantNumber` resolves a compile-time constant number (literal,
//     negated literal, or a const reference to another const number).
//   * `knownTypedLength` computes the known element count of a typed-array
//     expression, including from a const binding's recorded length.
//   * `typedIndexProvedInRange` proves an index is in range: either a
//     literal index smaller than the known length, or the induction variable
//     of a recognized for-loop whose bound does not exceed the array's known
//     length. In both cases the read type is `number` and codegen emits
//     `typedArrayAtChecked` with a runtime check as a safety net.
//   * `forLoopRange` recognizes the pattern `for (let i: i64 = 0; i < N; i =
//     i + 1)` (and `<=`, `+= 1`, `i++`) whose body never writes `i`; it
//     returns the induction variable and an exclusive upper bound.
//
// Ownership: this unit owns the proof machinery; `sema.cpp` owns the family
// model and the indexed-read dispatch (typedIndexRead forwards to here).
//
// Soundness, precisely: only the induction variable of a recognized loop is
// proven in range, only when the array binding is `const` so its length can
// never change, and only when the loop body does not write the index nor pass
// it to a call (a `ref` parameter could mutate it). The runtime bounds check
// in `typedArrayAtChecked` is never removed, so an unsound proof panics at
// runtime instead of reading out of bounds.
// =============================================================================
#include "detail/analyzer.h"

#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "cp3/host/native_types.hpp"
#include "detail/parallel_check.h"

namespace cp3 {

namespace {

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

bool literalIndexValue(const Expr& e, double& out) {
    if (e.kind != NodeKind::NumberLiteral) return false;
    out = static_cast<const NumberLiteral&>(e).value;
    return out >= 0.0 && std::isfinite(out) && std::trunc(out) == out;
}

const Expr* constantConditionBound(const Expr* condition,
                                   const std::string& induction) {
    if (!condition || condition->kind != NodeKind::BinaryExpr) return nullptr;
    const auto& binary = static_cast<const BinaryExpr&>(*condition);
    if (!binary.left || !binary.right) return nullptr;
    const bool left = binary.left->kind == NodeKind::Identifier &&
        static_cast<const Identifier&>(*binary.left).name == induction;
    const bool right = binary.right->kind == NodeKind::Identifier &&
        static_cast<const Identifier&>(*binary.right).name == induction;
    const bool forward = left && (binary.op == BinaryOp::LessThan ||
                                  binary.op == BinaryOp::LessThanEqual);
    const bool reverse = right && (binary.op == BinaryOp::GreaterThan ||
                                   binary.op == BinaryOp::GreaterThanEqual);
    if (!forward && !reverse) return nullptr;
    return forward ? binary.right.get() : binary.left.get();
}

// True when the update advances `name` by exactly one.
bool isIncrementByOne(const Expr& update, const std::string& name) {
    if (update.kind == NodeKind::UpdateExpr) {
        const auto& u = static_cast<const UpdateExpr&>(update);
        return u.op == UpdateOp::Increment && u.operand &&
               u.operand->kind == NodeKind::Identifier &&
               static_cast<const Identifier&>(*u.operand).name == name;
    }
    if (update.kind != NodeKind::AssignmentExpr) return false;
    const auto& a = static_cast<const AssignmentExpr&>(update);
    if (!a.target || a.target->kind != NodeKind::Identifier ||
        static_cast<const Identifier&>(*a.target).name != name) {
        return false;
    }
    if (a.op == AssignOp::Add && a.value &&
        a.value->kind == NodeKind::NumberLiteral &&
        static_cast<const NumberLiteral&>(*a.value).value == 1.0) {
        // `i += 1`.
        return true;
    }
    if (a.op != AssignOp::Assign || !a.value) return false;
    const Expr& v = *a.value;
    if (v.kind != NodeKind::BinaryExpr) return false;
    const auto& be = static_cast<const BinaryExpr&>(v);
    if (be.op != BinaryOp::Add) return false;
    auto isName = [&name](const Expr* e) -> bool {
        return e && e->kind == NodeKind::Identifier &&
               static_cast<const Identifier&>(*e).name == name;
    };
    auto isOne = [](const Expr* e) -> bool {
        if (!e || e->kind != NodeKind::NumberLiteral) return false;
        const auto& nl = static_cast<const NumberLiteral&>(*e);
        return nl.value == 1.0;
    };
    return (isName(be.left.get()) && isOne(be.right.get())) ||
           (isOne(be.left.get()) && isName(be.right.get()));
}

// The exclusive upper bound the induction variable cannot reach, derived from
// the recognized loop condition.
double upperBoundFromCondition(BinaryOp op, double bound) {
    if (op == BinaryOp::LessThanEqual) return std::floor(bound) + 1.0;
    // `< bound`: for an integer loop variable starting at 0 and advancing by
    // 1, the last reachable value is floor(bound); the first *stop* value is
    // ceil(bound), which is a safe exclusive ceiling.
    return std::ceil(bound);
}

}  // namespace

// ----------------------------------------------------------------------------
// Constant numbers
// ----------------------------------------------------------------------------
std::optional<double> Analyzer::constantNumber(const Expr& e) const {
    if (e.kind == NodeKind::NumberLiteral) {
        return static_cast<const NumberLiteral&>(e).value;
    }
    if (e.kind == NodeKind::UnaryExpr) {
        const auto& u = static_cast<const UnaryExpr&>(e);
        if (u.op == UnaryOp::Negate && u.operand) {
            if (auto v = constantNumber(*u.operand)) return -*v;
        }
    }
    if (e.kind == NodeKind::Identifier) {
        Binding b;
        if (lookup(static_cast<const Identifier&>(e).name, b) && b.isConst &&
            b.constNumber) {
            return b.constNumber;
        }
    }
    return std::nullopt;
}

// ----------------------------------------------------------------------------
// Typed-array lengths
// ----------------------------------------------------------------------------
std::optional<double> Analyzer::knownTypedLength(const Expr& e) const {
    if (e.kind == NodeKind::Identifier) {
        // A const binding's length was recorded at declaration; a `let` or a
        // `const` view of unknown length has none.
        Binding b;
        if (lookup(static_cast<const Identifier&>(e).name, b) &&
            b.typedLength) {
            return b.typedLength;
        }
        return std::nullopt;
    }
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
        if (args->size() == 3 && args->back() &&
            args->back()->kind == NodeKind::NumberLiteral) {
            double lit = static_cast<const NumberLiteral&>(*args->back()).value;
            if (lit >= 0.0) return std::trunc(lit);
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

// ----------------------------------------------------------------------------
// Proving an index is in range
// ----------------------------------------------------------------------------
bool Analyzer::typedIndexProvedInRange(const IndexExpr& ix) const {
    if (!ix.index || !ix.object) return false;
    // We need a statically known length: literal expressions, and const
    // typed-array bindings.
    const std::optional<double> len = knownTypedLength(*ix.object);
    if (!len) return false;
    double idx = 0.0;
    if (literalIndexValue(*ix.index, idx)) return idx < *len;
    if (ix.index->kind != NodeKind::Identifier) return false;
    const std::string& name =
        static_cast<const Identifier&>(*ix.index).name;
    // The loop proof needs a const array binding: its length can never change
    // (a non-const binding is not tracked), and a non-identifier receiver
    // (a method call) has no known length.
    if (ix.object->kind != NodeKind::Identifier) return false;
    Binding b;
    if (!lookup(static_cast<const Identifier&>(*ix.object).name, b) ||
        !b.isConst) {
        return false;
    }
    // Walk the innermost loop facts that mention this variable.
    for (auto it = indexRanges_.rbegin(); it != indexRanges_.rend(); ++it) {
        if (it->induction == name) return it->upper <= *len;
    }
    return false;
}

// ----------------------------------------------------------------------------
// for-loop induction analysis (called from stmt_check.cpp)
// ----------------------------------------------------------------------------
std::optional<Analyzer::IndexRangeFact> Analyzer::forLoopRange(
    ForStatement& stmt) {
    // init must be `let i = 0`.
    if (!stmt.init || stmt.init->kind != NodeKind::VariableDeclaration) {
        return std::nullopt;
    }
    auto& vd = static_cast<VariableDeclaration&>(*stmt.init);
    if (vd.declarations.size() != 1 || !vd.declarations.front()) {
        return std::nullopt;
    }
    auto& d = *vd.declarations.front();
    if (!d.id || d.id->kind != NodeKind::Identifier || !d.init) {
        return std::nullopt;
    }
    const std::string name =
        static_cast<const Identifier&>(*d.id).name;
    // The initializer must be exactly 0.
    if (auto v = constantNumber(*d.init); !v || *v != 0.0) {
        return std::nullopt;
    }
    // The loop variable must be a native integer: only integers have a total
    // order with a predictable increment, and a typed-array index is an
    // integer.
    Binding b;
    if (!lookup(name, b) || !isNativeInteger(b.type)) {
        return std::nullopt;
    }
    // Condition must be `i < N` or `i <= N` with a constant bound.
    const Expr* boundExpr = constantConditionBound(stmt.condition.get(), name);
    if (!boundExpr) return std::nullopt;
    const auto bound = constantNumber(*boundExpr);
    if (!bound) return std::nullopt;
    // Non-finite bounds make the proof useless.
    if (!std::isfinite(*bound)) return std::nullopt;
    const auto& condition = static_cast<const BinaryExpr&>(*stmt.condition);
    double upper = upperBoundFromCondition(condition.op, *bound);
    // The update must increment `i` by exactly 1.
    if (!stmt.update || !isIncrementByOne(*stmt.update, name)) {
        return std::nullopt;
    }
    // The body must not write the induction variable and must not pass it to a
    // call (a `ref` parameter could mutate it); it also must not declare a
    // binding with the same name.
    if (!stmt.body) return std::nullopt;
    parallel_detail::ClosureScanner bodyScanner(
        name, [](const std::string&) { return true; });
    if (bodyScanner.writesName(*stmt.body, name)) {
        return std::nullopt;
    }
    return IndexRangeFact{name, upper};
}

}  // namespace cp3
