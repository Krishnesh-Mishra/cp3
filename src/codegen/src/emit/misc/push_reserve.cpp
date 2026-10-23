// =============================================================================
// src/codegen/src/emit/push_reserve.cpp - capacity hints for counted push
// loops (STYLE.md section 8).
//
// A loop of the shape
//
//     while (i < bound) { a.push(value); i = i + 1; }
//
// performs the bound-minus-i pushes it has left, one per iteration. Emitting
// one `arrayReservePush(a, bound - i)` before the loop lets the vector allocate
// once instead of growing geometrically. Capacity is not an observable cp3
// value: the hint cannot change sizes, element values, aliasing or lifetimes,
// and `arrayReservePush` ignores a failed reservation. The pattern is matched
// structurally and conservatively: a plain counter compared with `<` or `<=`, a
// block of exactly one `push` statement and one unit increment of that counter.
// If anything else is in the body (a break, a second push, a nested call) the
// hint is skipped, because then the count would no longer describe the pushes.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

const Identifier* asIdentifier(const Expr* e) {
    if (!e || e->kind != NodeKind::Identifier) return nullptr;
    return static_cast<const Identifier*>(e);
}

// `i++` or `i = i + 1`, with the same identifier on both sides.
bool isUnitIncrement(const Expr& e, const std::string& counter) {
    if (e.kind == NodeKind::UpdateExpr) {
        const auto& u = static_cast<const UpdateExpr&>(e);
        const Identifier* target = asIdentifier(u.operand.get());
        return u.op == UpdateOp::Increment && target && target->name == counter;
    }
    if (e.kind != NodeKind::AssignmentExpr) return false;
    const auto& a = static_cast<const AssignmentExpr&>(e);
    if (a.op != AssignOp::Assign) return false;
    const Identifier* target = asIdentifier(a.target.get());
    if (!target || target->name != counter) return false;
    if (!a.value || a.value->kind != NodeKind::BinaryExpr) return false;
    const auto& sum = static_cast<const BinaryExpr&>(*a.value);
    if (sum.op != BinaryOp::Add) return false;
    const Identifier* lhs = asIdentifier(sum.left.get());
    if (!lhs || lhs->name != counter || !sum.right) return false;
    if (sum.right->kind != NodeKind::NumberLiteral) return false;
    return static_cast<const NumberLiteral&>(*sum.right).value == 1.0;
}

// One `a.push(value)` expression statement. `array` receives a's identifier.
bool isPushStatement(const Stmt& s, const Identifier*& array) {
    if (s.kind != NodeKind::ExpressionStatement) return false;
    const auto& es = static_cast<const ExpressionStatement&>(s);
    if (!es.expression || es.expression->kind != NodeKind::CallExpr) return false;
    const auto& call = static_cast<const CallExpr&>(*es.expression);
    if (!call.callee || call.callee->kind != NodeKind::MemberExpr) return false;
    const auto& member = static_cast<const MemberExpr&>(*call.callee);
    if (member.property != "push" || call.arguments.size() != 1) return false;
    const Identifier* recv = asIdentifier(member.object.get());
    if (!recv || !call.arguments.front()) return false;
    if (call.arguments.front()->kind == NodeKind::SpreadElement) return false;
    array = recv;
    return true;
}

bool isIncrementStatement(const Stmt& s, const std::string& counter) {
    if (s.kind != NodeKind::ExpressionStatement) return false;
    const auto& es = static_cast<const ExpressionStatement&>(s);
    return es.expression && isUnitIncrement(*es.expression, counter);
}

// The exact two-statement body of a counted push loop, in either order.
bool matchBody(const WhileStatement& ws, const std::string& counter,
               const Identifier*& array) {
    if (!ws.body || ws.body->kind != NodeKind::BlockStatement) return false;
    const auto& block = static_cast<const BlockStatement&>(*ws.body);
    if (block.body.size() != 2 || !block.body[0] || !block.body[1]) return false;
    const Stmt& first = *block.body[0];
    const Stmt& second = *block.body[1];
    const Identifier* pushed = nullptr;
    if (isPushStatement(first, pushed)) {
        if (!isIncrementStatement(second, counter)) return false;
        array = pushed;
        return true;
    }
    if (isPushStatement(second, pushed)) {
        if (!isIncrementStatement(first, counter)) return false;
        array = pushed;
        return true;
    }
    return false;
}

}  // namespace

namespace detail {

struct CountedPush {
    std::string array;
    std::string count;
};

struct PushReserveEmitter {
    explicit PushReserveEmitter(Generator& generator) : g(generator) {}

    Generator& g;

    bool run(const WhileStatement& ws, CountedPush& found);
    bool condition(const WhileStatement& ws, const std::string& counter,
                   std::string& count);
};

// The condition `counter < bound` (or `<=`) with an identifier or numeric
// literal bound; other conditions have no simple remaining count.
bool PushReserveEmitter::condition(const WhileStatement& ws,
                                   const std::string& counter,
                                   std::string& count) {
    if (!ws.condition || ws.condition->kind != NodeKind::BinaryExpr) return false;
    const auto& b = static_cast<const BinaryExpr&>(*ws.condition);
    if (b.op != BinaryOp::LessThan && b.op != BinaryOp::LessThanEqual) {
        return false;
    }
    const Identifier* lhs = asIdentifier(b.left.get());
    if (!lhs || lhs->name != counter || !b.right) return false;
    if (b.right->kind != NodeKind::Identifier &&
        b.right->kind != NodeKind::NumberLiteral) {
        return false;
    }
    const std::string bound = g.expr(*b.right);
    const std::string current = g.expr(*b.left);
    count = "(" + bound + " - " + current +
            (b.op == BinaryOp::LessThanEqual ? " + 1.0" : "") + ")";
    return true;
}

bool PushReserveEmitter::run(const WhileStatement& ws, CountedPush& found) {
    // The int64 fast body is a speculative lowering; a capacity hint there adds
    // no value and could move a bail point, so leave those bodies alone.
    if (g.intOn()) return false;
    if (!ws.condition || ws.condition->kind != NodeKind::BinaryExpr) return false;
    const auto& b = static_cast<const BinaryExpr&>(*ws.condition);
    if (b.op != BinaryOp::LessThan && b.op != BinaryOp::LessThanEqual) {
        return false;
    }
    const Identifier* counter = asIdentifier(b.left.get());
    if (!counter) return false;
    const Identifier* array = nullptr;
    if (!matchBody(ws, counter->name, array) || !array) return false;
    if (array->name == counter->name) return false;
    const auto recv = g.lookup(array->name);
    if (!recv || recv->cell || classify(recv->type).shape != Shape::Array) {
        return false;
    }
    if (!g.lookup(counter->name)) return false;
    if (!condition(ws, counter->name, found.count)) return false;
    found.array = g.expr(*array);
    return true;
}

}  // namespace detail

void Generator::emitPushReserveHint(const WhileStatement& ws, std::string& out,
                                    int indent) {
    detail::CountedPush found;
    if (!detail::PushReserveEmitter(*this).run(ws, found)) return;
    line(out, indent, "::cp3::runtime::arrayReservePush(" + found.array + ", " +
                          found.count + ");");
}

}  // namespace cp3
