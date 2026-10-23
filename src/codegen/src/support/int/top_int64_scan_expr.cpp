// =============================================================================
// src/codegen/src/support/top_int64_scan_expr.cpp - the expression half of the
// whole-unit walk behind the entry-body int64 proof.
//
// The walk records every binding declaration, every reference, every write and
// every `while` loop of the unit. It is deliberately exhaustive: a construct
// it does not model turns `complete` false, and the proof then rejects every
// candidate in the unit (complete-or-nothing, like int_reads.cpp).
//
// The statement half lives in support/top_int64_scan_stmt.cpp.
// =============================================================================
#include "detail/int/top_int64_scan.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cp3 {
namespace detail {

namespace {

using Scan = TopInt64Scan;

void scanExpr(const Expr& e, Scan& s);

// The literal-ish leaves: nothing to record beyond the fact that they are
// values.
void scanLeafExpr(const Expr& e, Scan& s) {
    switch (e.kind) {
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
        case NodeKind::ThisExpr:
        case NodeKind::RegExpLiteral:
            return;
        case NodeKind::Identifier:
            noteTopInt64Ref(e, static_cast<const Identifier&>(e).name, s);
            return;
        default:
            return;
    }
}

// Template / array / object / spread / weak: a value built from other values.
void scanValueExpr(const Expr& e, Scan& s) {
    if (e.kind == NodeKind::TemplateLiteral) {
        scanTopInt64ExprList(static_cast<const TemplateLiteral&>(e).expressions, s);
        return;
    }
    if (e.kind == NodeKind::ArrayLiteral) {
        scanTopInt64ExprList(static_cast<const ArrayLiteral&>(e).elements, s);
        return;
    }
    if (e.kind == NodeKind::ObjectLiteral) {
        for (const auto& p : static_cast<const ObjectLiteral&>(e).properties) {
            if (!p) {
                s.complete = false;
                continue;
            }
            if (p->computed && p->key) scanExpr(*p->key, s);
            if (p->value) scanExpr(*p->value, s);
        }
        return;
    }
    if (e.kind == NodeKind::SpreadElement) {
        const auto& sp = static_cast<const SpreadElement&>(e);
        if (sp.argument) scanExpr(*sp.argument, s);
        return;
    }
    const auto& w = static_cast<const WeakExpr&>(e);
    if (w.value) scanExpr(*w.value, s);
}

// Member / index access: the receiver and the key are ordinary reads.
void scanAccessExpr(const Expr& e, Scan& s) {
    if (e.kind == NodeKind::MemberExpr) {
        const auto& m = static_cast<const MemberExpr&>(e);
        if (m.object) scanExpr(*m.object, s);
        return;
    }
    const auto& ix = static_cast<const IndexExpr&>(e);
    if (ix.object) scanExpr(*ix.object, s);
    if (ix.index) scanExpr(*ix.index, s);
}

// A call or a construction: the callee and the arguments are ordinary reads.
void scanCallExpr(const Expr& e, Scan& s) {
    if (e.kind == NodeKind::CallExpr) {
        const auto& c = static_cast<const CallExpr&>(e);
        if (c.callee) scanExpr(*c.callee, s);
        scanTopInt64ExprList(c.arguments, s);
        return;
    }
    const auto& n = static_cast<const NewExpr&>(e);
    if (n.callee) scanExpr(*n.callee, s);
    scanTopInt64ExprList(n.arguments, s);
}

// Unary / update / binary / logical / conditional / await / range.
void scanOperatorExpr(const Expr& e, Scan& s) {
    switch (e.kind) {
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(e);
            if (u.operand) scanExpr(*u.operand, s);
            return;
        }
        case NodeKind::UpdateExpr: {
            const auto& u = static_cast<const UpdateExpr&>(e);
            if (!u.operand) {
                s.complete = false;
                return;
            }
            if (u.operand->kind == NodeKind::Identifier) {
                s.updates.push_back(&u);
                if (s.inFrame) s.frameUpdates.insert(&u);
                noteTopInt64Ref(e, static_cast<const Identifier&>(*u.operand).name, s);
                return;
            }
            scanExpr(*u.operand, s);
            return;
        }
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            if (b.left) scanExpr(*b.left, s);
            if (b.right) scanExpr(*b.right, s);
            return;
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(e);
            if (l.left) scanExpr(*l.left, s);
            if (l.right) scanExpr(*l.right, s);
            return;
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(e);
            if (c.condition) scanExpr(*c.condition, s);
            if (c.whenTrue) scanExpr(*c.whenTrue, s);
            if (c.whenFalse) scanExpr(*c.whenFalse, s);
            return;
        }
        case NodeKind::AwaitExpr: {
            const auto& a = static_cast<const AwaitExpr&>(e);
            if (a.argument) scanExpr(*a.argument, s);
            return;
        }
        default: {
            const auto& r = static_cast<const RangeExpr&>(e);
            if (r.start) scanExpr(*r.start, s);
            if (r.end) scanExpr(*r.end, s);
            return;
        }
    }
}

// A write: an identifier target is a binding write, a member / index target is
// a write through a value, and a pattern target is not modelled.
void scanAssignExpr(const Expr& e, Scan& s) {
    const auto& a = static_cast<const AssignmentExpr&>(e);
    if (!a.target || !a.value) {
        s.complete = false;
        return;
    }
    if (a.target->kind == NodeKind::Identifier) {
        s.writes.push_back(&a);
        if (s.inFrame) s.frameWrites.insert(&a);
        noteTopInt64Ref(e, static_cast<const Identifier&>(*a.target).name, s);
    } else if (a.target->kind == NodeKind::MemberExpr ||
               a.target->kind == NodeKind::IndexExpr) {
        scanExpr(*a.target, s);
    } else {
        s.complete = false;  // a pattern assignment
    }
    scanExpr(*a.value, s);
}

// An arrow: a deferred body, and a reader of the module scope from outside
// the entry statement flow.
void scanArrowExpr(const Expr& e, Scan& s) {
    const auto& af = static_cast<const ArrowFunction&>(e);
    // A function value at module scope is a reader/writer outside the entry
    // statement flow, and the `main`-local lowering cannot give it a
    // namespace-scope spelling. Keep module bindings globals.
    s.complete = false;
    const bool saved = s.inFrame;
    s.inFrame = true;
    scanTopInt64Params(af.params, s);
    if (!af.body) {
        s.complete = false;
    } else if (af.body->kind == NodeKind::BlockStatement ||
               dynamic_cast<const Stmt*>(af.body.get()) != nullptr) {
        scanTopInt64Stmt(static_cast<const Stmt&>(*af.body), s);
    } else if (auto* body = dynamic_cast<const Expr*>(af.body.get())) {
        scanExpr(*body, s);
    } else {
        s.complete = false;
    }
    s.inFrame = saved;
}

void scanExpr(const Expr& e, Scan& s) {
    switch (e.kind) {
        case NodeKind::NumberLiteral:
        case NodeKind::StringLiteral:
        case NodeKind::BoolLiteral:
        case NodeKind::NullLiteral:
        case NodeKind::UndefinedLiteral:
        case NodeKind::ThisExpr:
        case NodeKind::RegExpLiteral:
        case NodeKind::Identifier:
            scanLeafExpr(e, s);
            return;
        case NodeKind::TemplateLiteral:
        case NodeKind::ArrayLiteral:
        case NodeKind::ObjectLiteral:
        case NodeKind::SpreadElement:
        case NodeKind::WeakExpr:
            scanValueExpr(e, s);
            return;
        case NodeKind::MemberExpr:
        case NodeKind::IndexExpr:
            scanAccessExpr(e, s);
            return;
        case NodeKind::CallExpr:
        case NodeKind::NewExpr:
            scanCallExpr(e, s);
            return;
        case NodeKind::UnaryExpr:
        case NodeKind::UpdateExpr:
        case NodeKind::BinaryExpr:
        case NodeKind::LogicalExpr:
        case NodeKind::ConditionalExpr:
        case NodeKind::AwaitExpr:
        case NodeKind::RangeExpr:
            scanOperatorExpr(e, s);
            return;
        case NodeKind::AssignmentExpr:
            scanAssignExpr(e, s);
            return;
        case NodeKind::ArrowFunction:
            scanArrowExpr(e, s);
            return;
        default:
            // Patterns and any future expression kind: not modelled.
            s.complete = false;
            return;
    }
}

}  // namespace

void noteTopInt64Ref(const Node& n, const std::string& name, TopInt64Scan& s) {
    s.idents.insert(name);
    if (!n.span.valid()) {
        s.complete = false;  // an unplaceable reference cannot be ordered
        return;
    }
    const std::uint32_t offset = n.span.begin.offset;
    const auto it = s.firstRef.find(name);
    if (it == s.firstRef.end() || offset < it->second) {
        s.firstRef[name] = offset;
    }
}

void scanTopInt64Name(const Node* id, TopInt64Scan& s) {
    if (id == nullptr) return;  // `catch {}` has no binding
    if (id->kind == NodeKind::Identifier) {
        const std::string& name = static_cast<const Identifier&>(*id).name;
        ++s.bindings[name];
        s.idents.insert(name);
        return;
    }
    s.complete = false;  // a destructuring pattern
}

void scanTopInt64Params(const ParamList& params, TopInt64Scan& s) {
    for (const auto& p : params) {
        if (!p) {
            s.complete = false;
            continue;
        }
        scanTopInt64Name(p->name.get(), s);
        if (p->defaultValue) scanExpr(*p->defaultValue, s);
    }
}

void scanTopInt64ExprList(const ExprList& list, TopInt64Scan& s) {
    for (const auto& e : list) {
        if (e) scanExpr(*e, s);
    }
}

void scanTopInt64Expr(const Expr& e, TopInt64Scan& s) { scanExpr(e, s); }

}  // namespace detail
}  // namespace cp3
