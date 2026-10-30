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
// exactly one push site per iteration (including one side-effect-free
// conditional push), and one unit increment of that counter. A conditional
// push reserves the iteration count as an upper bound. Break, continue,
// multiple push sites and nested loops skip the hint.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"
#include "detail/class/class_model.h"

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
        case NodeKind::MemberExpr: {
            const auto& m = static_cast<const MemberExpr&>(e);
            return !m.optional && m.object && isSimpleValue(*m.object);
        }
        case NodeKind::IndexExpr: {
            const auto& ix = static_cast<const IndexExpr&>(e);
            return ix.object && ix.index && isSimpleValue(*ix.object) &&
                   isSimpleValue(*ix.index);
        }
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

// Constructors accepted for moving a reserve hint ahead of the loop: a
// default or explicit constructor that only writes its own fields from simple
// values. Such construction cannot mutate the induction variable, bound, or
// array being reserved through a call or capture.
bool isSafeNew(const Expr& e) {
    if (e.kind != NodeKind::NewExpr) return false;
    const auto& n = static_cast<const NewExpr&>(e);
    const Identifier* callee = asIdentifier(n.callee.get());
    if (!callee) return false;
    for (const ExprPtr& argument : n.arguments) {
        if (!argument || !isSimpleValue(*argument)) return false;
    }
    const ClassModel* model = ModelRegistry::get().classByName(callee->name);
    if (!model || !model->decl || !model->baseName.empty()) return false;
    const ConstructorDeclaration* constructor = nullptr;
    for (const ClassMemberModel& member : model->members) {
        if (member.hasInitializer || member.isWeak) return false;
        if (member.ctor) {
            if (constructor) return false;
            constructor = member.ctor;
        }
    }
    if (!constructor || !constructor->body) return true;
    if (constructor->body->kind != NodeKind::BlockStatement) return false;
    const auto& body = static_cast<const BlockStatement&>(*constructor->body);
    for (const StmtPtr& statement : body.body) {
        if (!statement || statement->kind != NodeKind::ExpressionStatement) return false;
        const auto& expression = static_cast<const ExpressionStatement&>(*statement);
        if (!expression.expression ||
            expression.expression->kind != NodeKind::AssignmentExpr) return false;
        const auto& assignment = static_cast<const AssignmentExpr&>(*expression.expression);
        if (assignment.op != AssignOp::Assign || !assignment.target ||
            assignment.target->kind != NodeKind::MemberExpr || !assignment.value ||
            !isSimpleValue(*assignment.value)) return false;
        const auto& target = static_cast<const MemberExpr&>(*assignment.target);
        if (!target.object || target.object->kind != NodeKind::ThisExpr) return false;
        bool field = false;
        for (const ClassMemberModel& member : model->members) {
            if (member.name == target.property && member.field && !member.isWeak) {
                field = true;
                break;
            }
        }
        if (!field) return false;
    }
    return true;
}

bool isSafePush(const Stmt& statement, const Identifier*& array) {
    if (!isPushStatement(statement, array)) return false;
    const auto& call = static_cast<const CallExpr&>(
        *static_cast<const ExpressionStatement&>(statement).expression);
    const Expr& value = *call.arguments.front();
    return isSimpleValue(value) || isSafeNew(value);
}

bool isSinglePushBranch(const Stmt* statement, const Identifier*& array) {
    if (!statement) return false;
    if (statement->kind == NodeKind::ExpressionStatement) {
        return isSafePush(*statement, array);
    }
    if (statement->kind != NodeKind::BlockStatement) return false;
    const auto& block = static_cast<const BlockStatement&>(*statement);
    return block.body.size() == 1 && block.body.front() &&
           isSinglePushBranch(block.body.front().get(), array);
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
    unsigned pushSites = 0;
    bool sawIncrement = false;
    std::vector<const Expr*> extras;
    std::vector<std::string> declaredNames;
    for (const auto& stmt : block.body) {
        if (!stmt) return false;
        if (stmt->kind == NodeKind::EmptyStatement) continue;
        if (stmt->kind == NodeKind::IfStatement) {
            const auto& branch = static_cast<const IfStatement&>(*stmt);
            const Identifier* receiver = nullptr;
            if (!branch.condition || !isSimpleValue(*branch.condition) ||
                branch.elseBranch ||
                !isSinglePushBranch(branch.thenBranch.get(), receiver) ||
                !receiver || (pushed && pushed->name != receiver->name)) {
                return false;
            }
            pushed = receiver;
            ++pushSites;
            continue;
        }
        if (stmt->kind == NodeKind::ExpressionStatement) {
            const auto& es = static_cast<const ExpressionStatement&>(*stmt);
            if (!es.expression) return false;
            const Identifier* receiver = nullptr;
            if (isSafePush(*stmt, receiver)) {
                if ((pushed && pushed->name != receiver->name) || ++pushSites > 1) {
                    return false;
                }
                pushed = receiver;
                continue;
            }
            if (isIncrementStatement(*stmt, counter)) {
                if (sawIncrement) return false;
                sawIncrement = true;
                continue;
            }
            extras.push_back(es.expression.get());
            continue;
        }
        if (stmt->kind == NodeKind::VariableDeclaration) {
            const auto& declaration = static_cast<const VariableDeclaration&>(*stmt);
            if (declaration.declarations.empty()) return false;
            for (const VariableDeclaratorPtr& item : declaration.declarations) {
                if (!item || !item->id || item->id->kind != NodeKind::Identifier ||
                    !item->init) return false;
                const std::string& name =
                    static_cast<const Identifier&>(*item->id).name;
                if (name == counter || name == bound ||
                    (!isSimpleValue(*item->init) && !isSafeNew(*item->init))) {
                    return false;
                }
                declaredNames.push_back(name);
            }
            continue;
        }
        return false;
    }
    if (pushSites != 1 || !sawIncrement || !pushed) return false;
    if (pushed->name == counter || pushed->name == bound) return false;
    for (const Expr* extra : extras) {
        if (!extra || !isAllowedExtraStatement(*extra, counter, pushed->name, bound)) {
            return false;
        }
    }
    for (const std::string& name : declaredNames) {
        if (name == pushed->name) return false;
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
