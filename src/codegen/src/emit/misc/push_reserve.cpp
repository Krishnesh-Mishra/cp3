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
// structurally and conservatively: a plain counter compared with `<` or `<=`,
// exactly one top-level `push` and one unit increment of that counter, plus
// only statements that cannot change the counter, the bound or the array (a
// running accumulator is the common case). A conditional, break, continue,
// second push or nested loop skips the hint, because then the count would no
// longer describe the pushes.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

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

// A value expression with no calls, allocations, member/index access or
// nested assignment. These are the only extra statements allowed in a counted
// push body: they cannot change the counter, the bound, or the array binding.
bool isSimpleValue(const Expr& e) {
    switch (e.kind) {
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
        case NodeKind::Identifier:
        case NodeKind::ThisExpr:
            return true;
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            return b.left && b.right && isSimpleValue(*b.left) &&
                   isSimpleValue(*b.right);
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(e);
            return l.left && l.right && isSimpleValue(*l.left) &&
                   isSimpleValue(*l.right);
        }
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(e);
            return u.operand && isSimpleValue(*u.operand);
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(e);
            return c.condition && c.whenTrue && c.whenFalse &&
                   isSimpleValue(*c.condition) && isSimpleValue(*c.whenTrue) &&
                   isSimpleValue(*c.whenFalse);
        }
        default:
            return false;
    }
}

// An extra statement may update an unrelated accumulator (`total = total + i`)
// but never the loop counter, the bound, or the array being pushed.
bool isAllowedExtraStatement(const Expr& e, const std::string& counter,
                             const std::string& array,
                             const std::string& bound) {
    if (e.kind == NodeKind::AssignmentExpr) {
        const auto& a = static_cast<const AssignmentExpr&>(e);
        const Identifier* target = asIdentifier(a.target.get());
        if (!target) return false;
        if (target->name == counter || target->name == array ||
            (!bound.empty() && target->name == bound)) {
            return false;
        }
        return a.value && isSimpleValue(*a.value);
    }
    if (e.kind == NodeKind::UpdateExpr) {
        const auto& u = static_cast<const UpdateExpr&>(e);
        const Identifier* target = asIdentifier(u.operand.get());
        if (!target) return false;
        return target->name != counter && target->name != array &&
               (bound.empty() || target->name != bound);
    }
    return isSimpleValue(e);
}

// The counted push body: exactly one top-level push of the array and one unit
// increment of the counter, plus statements that cannot affect either. The
// extra statements are what the original two-statement matcher missed (for
// example array-growth's running `total` update).
bool matchBody(const WhileStatement& ws, const std::string& counter,
               const std::string& bound, const Identifier*& array) {
    if (!ws.body || ws.body->kind != NodeKind::BlockStatement) return false;
    const auto& block = static_cast<const BlockStatement&>(*ws.body);
    const Identifier* pushed = nullptr;
    bool sawPush = false;
    bool sawIncrement = false;
    std::vector<const Expr*> extras;
    for (const auto& stmt : block.body) {
        if (!stmt) return false;
        if (stmt->kind == NodeKind::EmptyStatement) continue;
        if (stmt->kind != NodeKind::ExpressionStatement) return false;
        const auto& es = static_cast<const ExpressionStatement&>(*stmt);
        if (!es.expression) return false;
        const Identifier* receiver = nullptr;
        if (isPushStatement(*stmt, receiver)) {
            if (sawPush) return false;
            sawPush = true;
            pushed = receiver;
            continue;
        }
        if (isIncrementStatement(*stmt, counter)) {
            if (sawIncrement) return false;
            sawIncrement = true;
            continue;
        }
        extras.push_back(es.expression.get());
    }
    if (!sawPush || !sawIncrement || !pushed) return false;
    for (const Expr* extra : extras) {
        if (!extra ||
            !isAllowedExtraStatement(*extra, counter, pushed->name, bound)) {
            return false;
        }
    }
    array = pushed;
    return true;
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
                   std::string& count, std::string& bound);
};

// The condition `counter < bound` (or `<=`) with an identifier or numeric
// literal bound; other conditions have no simple remaining count.
bool PushReserveEmitter::condition(const WhileStatement& ws,
                                   const std::string& counter,
                                   std::string& count, std::string& bound) {
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
    bound.clear();
    if (b.right->kind == NodeKind::Identifier) {
        bound = static_cast<const Identifier&>(*b.right).name;
    }
    const std::string boundText = g.expr(*b.right);
    const std::string current = g.expr(*b.left);
    count = "(" + boundText + " - " + current +
            (b.op == BinaryOp::LessThanEqual ? " + 1.0" : "") + ")";
    return true;
}

bool PushReserveEmitter::run(const WhileStatement& ws, CountedPush& found) {
    if (!ws.condition || ws.condition->kind != NodeKind::BinaryExpr) return false;
    const auto& b = static_cast<const BinaryExpr&>(*ws.condition);
    if (b.op != BinaryOp::LessThan && b.op != BinaryOp::LessThanEqual) {
        return false;
    }
    const Identifier* counter = asIdentifier(b.left.get());
    if (!counter) return false;
    std::string count;
    std::string bound;
    if (!condition(ws, counter->name, count, bound)) return false;
    const Identifier* array = nullptr;
    if (!matchBody(ws, counter->name, bound, array) || !array) return false;
    if (array->name == counter->name) return false;
    const auto recv = g.lookup(array->name);
    if (!recv || recv->cell || classify(recv->type).shape != Shape::Array) {
        return false;
    }
    if (!g.lookup(counter->name)) return false;
    // Capacity is not observable from cp3, so the hint is safe in the int64
    // fast body too: it only reserves, and a failed reservation is ignored.
    found.count = count;
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
