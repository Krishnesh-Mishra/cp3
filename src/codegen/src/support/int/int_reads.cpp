// =============================================================================
// src/codegen/src/support/int_reads.cpp - the read walk behind the deferred bound check
// of the int64 fast path (D64).
//
// The fast path may skip a range check only when it can name the single place
// where the value becomes observable again (`arith::pass` at the read, see
// cp3/core/arith.hpp). A `number` local whose reads all sit inside `return`
// statements has exactly that shape: the hot loop that grows it never tests it
// and never returns it, so the check moves from once per operation to once per
// return, and the loop body runs on the int64 ALU alone.
//
// A name counts as read unless it occurs in a write-only position: a plain `=`
// target or a declarator name. A read of the assignment's own target inside its
// value does not count either: that value is stored straight back (see the
// `x = x + 1` case below). Everything else - including `x++` and `x += 1` - is a
// read, and anything this walk does not model gives up on the whole body (see
// the header), so the answer is only ever trusted when it is complete.
// =============================================================================
#include "detail/int/int_reads.h"

namespace cp3 {

namespace {

bool walkExpr(const Expr& e, std::unordered_set<std::string>& out);
bool walkStmt(const Stmt& s, std::unordered_set<std::string>& out);
bool walkDecl(const VariableDeclaration& vd, std::unordered_set<std::string>& out);
bool walkFor(const ForStatement& f, std::unordered_set<std::string>& out);

bool walkExpr(const Expr& e, std::unordered_set<std::string>& out) {
    switch (e.kind) {
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
        case NodeKind::ThisExpr:
            return true;
        case NodeKind::Identifier:
            out.insert(static_cast<const Identifier&>(e).name);
            return true;
        case NodeKind::TemplateLiteral: {
            const auto& tl = static_cast<const TemplateLiteral&>(e);
            for (const auto& part : tl.expressions) {
                if (!part || !walkExpr(*part, out)) return false;
            }
            return true;
        }
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            return b.left && b.right && walkExpr(*b.left, out) && walkExpr(*b.right, out);
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(e);
            return l.left && l.right && walkExpr(*l.left, out) && walkExpr(*l.right, out);
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(e);
            return c.condition && c.whenTrue && c.whenFalse &&
                   walkExpr(*c.condition, out) && walkExpr(*c.whenTrue, out) &&
                   walkExpr(*c.whenFalse, out);
        }
        case NodeKind::UnaryExpr: {
            // Every unary operator reads its operand's value (`typeof` too).
            const auto& u = static_cast<const UnaryExpr&>(e);
            return u.operand && walkExpr(*u.operand, out);
        }
        case NodeKind::UpdateExpr: {
            const auto& u = static_cast<const UpdateExpr&>(e);
            return u.operand && walkExpr(*u.operand, out);
        }
        case NodeKind::AssignmentExpr: {
            const auto& a = static_cast<const AssignmentExpr&>(e);
            if (!a.target || !a.value) return false;
            if (a.op != AssignOp::Assign) {
                return walkExpr(*a.target, out) && walkExpr(*a.value, out);
            }
            if (a.target->kind != NodeKind::Identifier) return false;
            // `x = <v>` stores v back into x, so a read of x inside v is not an
            // observation: the assignment is either lowered with the checked
            // helpers, which re-check that read, or as a raw `+`/`-` whose
            // result goes straight back into x. Reads of anything else in v are
            // ordinary reads.
            std::unordered_set<std::string> valueReads;
            if (!walkExpr(*a.value, valueReads)) return false;
            valueReads.erase(static_cast<const Identifier&>(*a.target).name);
            out.insert(valueReads.begin(), valueReads.end());
            return true;
        }
        default:
            // Calls, members, indexes, literals of objects/arrays, arrows,
            // `await`, ranges: not modelled, so nothing is deferred.
            return false;
    }
}

bool walkDecl(const VariableDeclaration& vd, std::unordered_set<std::string>& out) {
    for (const auto& d : vd.declarations) {
        if (!d || !d->id || d->id->kind != NodeKind::Identifier) return false;
        if (d->init && !walkExpr(*d->init, out)) return false;
    }
    return true;
}

bool walkFor(const ForStatement& f, std::unordered_set<std::string>& out) {
    if (f.init) {
        if (f.init->kind == NodeKind::VariableDeclaration) {
            if (!walkDecl(static_cast<const VariableDeclaration&>(*f.init), out)) return false;
        } else if (auto exp = dynamic_cast<const Expr*>(f.init.get())) {
            if (!walkExpr(*exp, out)) return false;
        } else {
            return false;
        }
    }
    if (f.condition && !walkExpr(*f.condition, out)) return false;
    if (f.update && !walkExpr(*f.update, out)) return false;
    return f.body && walkStmt(*f.body, out);
}

bool walkStmt(const Stmt& s, std::unordered_set<std::string>& out) {
    switch (s.kind) {
        case NodeKind::EmptyStatement:
            return true;
        case NodeKind::ExpressionStatement: {
            const auto& es = static_cast<const ExpressionStatement&>(s);
            return es.expression && walkExpr(*es.expression, out);
        }
        case NodeKind::BlockStatement: {
            const auto& b = static_cast<const BlockStatement&>(s);
            for (const auto& inner : b.body) {
                if (!inner || !walkStmt(*inner, out)) return false;
            }
            return true;
        }
        case NodeKind::VariableDeclaration:
            return walkDecl(static_cast<const VariableDeclaration&>(s), out);
        case NodeKind::IfStatement: {
            const auto& i = static_cast<const IfStatement&>(s);
            if (!i.condition || !i.thenBranch) return false;
            if (!walkExpr(*i.condition, out) || !walkStmt(*i.thenBranch, out)) return false;
            return !i.elseBranch || walkStmt(*i.elseBranch, out);
        }
        case NodeKind::WhileStatement: {
            const auto& w = static_cast<const WhileStatement&>(s);
            if (!w.condition || !w.body) return false;
            return walkExpr(*w.condition, out) && walkStmt(*w.body, out);
        }
        case NodeKind::DoWhileStatement: {
            const auto& d = static_cast<const DoWhileStatement&>(s);
            if (!d.body || !d.condition) return false;
            return walkStmt(*d.body, out) && walkExpr(*d.condition, out);
        }
        case NodeKind::ForStatement:
            return walkFor(static_cast<const ForStatement&>(s), out);
        case NodeKind::ReturnStatement:
            // Reads inside a return are exactly the reads the fast path
            // re-checks with `arith::pass`, so they do not count here.
            return true;
        case NodeKind::BreakStatement:
        case NodeKind::ContinueStatement:
            return true;
        default:
            return false;  // throw, try, switch, for-of/for-in, labels, ...
    }
}

}  // namespace

std::optional<std::unordered_set<std::string>> namesReadOutsideReturns(const Stmt& body) {
    std::unordered_set<std::string> out;
    if (!walkStmt(body, out)) return std::nullopt;
    return out;
}

}  // namespace cp3
