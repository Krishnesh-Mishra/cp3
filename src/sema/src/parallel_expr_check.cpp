// =============================================================================
// src/sema/src/parallel_expr_check.cpp — parallel.* capture safety: expression
// walk and closure-local scope tracking.
//
// Expression half of the capture-safety walk declared in parallel_check.h and
// documented there and in parallel_check.cpp (docs/language-spec §13): every
// expression form is traversed so that a write through it (assignment or
// update target, field write, indexed write, mutating method call) is found,
// while plain reads pass through unchanged. Scope tracking records the names
// declared inside the closure, so a write to a local is never mistaken for a
// captured write. Statements live in parallel_check.cpp (STYLE.md §8).
// =============================================================================
#include "parallel_check.h"

#include <initializer_list>
#include <string>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {
namespace parallel_detail {

// =============================================================================
// Shared child-walk primitives
// =============================================================================
void ClosureScanner::scanMaybeExpr(Expr* e) {
    if (e) scanExpr(*e);
}

void ClosureScanner::scanAll(std::initializer_list<Expr*> items) {
    for (Expr* e : items) scanMaybeExpr(e);
}

// =============================================================================
// Closure-local scope tracking
// =============================================================================
void ClosureScanner::pushScope() { locals_.emplace_back(); }

void ClosureScanner::popScope() {
    if (!locals_.empty()) locals_.pop_back();
}

void ClosureScanner::declareName(const std::string& name) {
    if (!name.empty() && !locals_.empty()) locals_.back().insert(name);
}

// Records every name a binding pattern introduces, so a write to a closure
// local is not reported as a captured write.
void ClosureScanner::declarePattern(Node* node) {
    if (!node) return;
    switch (node->kind) {
        case NodeKind::Identifier:
            declareName(static_cast<Identifier&>(*node).name);
            return;
        case NodeKind::AssignmentPattern:
            declarePattern(static_cast<AssignmentPattern&>(*node).left.get());
            return;
        case NodeKind::RestElement:
            declarePattern(static_cast<RestElement&>(*node).argument.get());
            return;
        case NodeKind::ArrayPattern: {
            auto& ap = static_cast<ArrayPattern&>(*node);
            for (auto& el : ap.elements) declarePattern(el.get());
            if (ap.rest) declarePattern(ap.rest.get());
            return;
        }
        case NodeKind::ObjectPattern: {
            auto& op = static_cast<ObjectPattern&>(*node);
            for (auto& prop : op.properties) {
                if (prop) declarePattern(prop->value.get());
            }
            if (op.rest) declarePattern(op.rest.get());
            return;
        }
        default:
            return;
    }
}

bool ClosureScanner::isLocal(const std::string& name) const {
    for (const auto& scope : locals_) {
        if (scope.count(name) != 0) return true;
    }
    return false;
}

bool ClosureScanner::isCaptured(const std::string& name) const {
    return !isLocal(name) && resolves_(name);
}

// True when the index is parallel.for's own induction variable: the single
// provably disjoint write, so no diagnostic is produced for it.
bool ClosureScanner::throughInduction(const IndexExpr& ix) const {
    return !induction_.empty() && ix.index &&
           ix.index->kind == NodeKind::Identifier &&
           static_cast<Identifier&>(*ix.index).name == induction_;
}

void ClosureScanner::addCaptured(const std::string& name, const SourceSpan& span) {
    findings_.push_back(
        Finding{diagcode::kParallelCaptureReassign, span,
                "cannot reassign or mutate captured variable '" + name +
                    "' inside a parallel.* closure (captures are shared, "
                    "non-owning borrows); return a value and combine it on "
                    "the calling thread instead"});
}

void ClosureScanner::addIndex(const SourceSpan& span) {
    findings_.push_back(
        Finding{diagcode::kParallelSharedMutation, span,
                "unsafe indexed write to captured state inside a "
                "parallel.* closure: only parallel.for's own induction "
                "variable is allowed as the index (a computed index cannot "
                "be proven disjoint)"});
}

// =============================================================================
// Expression dispatch
// =============================================================================
// Every expression is walked; only assignment, update, call and member/index
// chains can reach a write, so those dispatch to the target-aware helpers and
// everything else is scanned read-only.
void ClosureScanner::scanExpr(Expr& e) {
    switch (e.kind) {
        case NodeKind::AssignmentExpr: {
            auto& a = static_cast<AssignmentExpr&>(e);
            scanMaybeExpr(a.value.get());
            if (a.target) scanTarget(*a.target);
            return;
        }
        case NodeKind::UpdateExpr: {
            auto& u = static_cast<UpdateExpr&>(e);
            if (u.operand) scanTarget(*u.operand);
            return;
        }
        case NodeKind::CallExpr:
            scanCall(static_cast<CallExpr&>(e));
            return;
        case NodeKind::ArrowFunction:
            // A nested closure belongs to this body: its writes to captured
            // state are writes performed by the same worker, so it is scanned
            // under this scanner's induction variable.
            scan(static_cast<ArrowFunction&>(e));
            return;
        default:
            scanExprChildren(e);
            return;
    }
}

// Read-only forms: no write can occur through them, so their children are
// scanned in source order and nothing is reported for the node itself.
void ClosureScanner::scanExprChildren(Expr& e) {
    switch (e.kind) {
        case NodeKind::BinaryExpr:
        case NodeKind::LogicalExpr:
        case NodeKind::ConditionalExpr:
        case NodeKind::IndexExpr:
        case NodeKind::RangeExpr:
            scanOperatorExprs(e);
            return;
        case NodeKind::MemberExpr:
        case NodeKind::UnaryExpr:
        case NodeKind::SpreadElement:
        case NodeKind::AwaitExpr:
        case NodeKind::WeakExpr:
            scanSingleChildExprs(e);
            return;
        case NodeKind::ArrayLiteral:
        case NodeKind::ObjectLiteral:
        case NodeKind::TemplateLiteral:
        case NodeKind::NewExpr:
            scanContainerExprs(e);
            return;
        default:
            return;
    }
}

// Operators with a fixed, ordered operand list.
void ClosureScanner::scanOperatorExprs(Expr& e) {
    switch (e.kind) {
        case NodeKind::BinaryExpr: {
            auto& b = static_cast<BinaryExpr&>(e);
            scanAll({b.left.get(), b.right.get()});
            return;
        }
        case NodeKind::LogicalExpr: {
            auto& l = static_cast<LogicalExpr&>(e);
            scanAll({l.left.get(), l.right.get()});
            return;
        }
        case NodeKind::ConditionalExpr: {
            auto& c = static_cast<ConditionalExpr&>(e);
            scanAll({c.condition.get(), c.whenTrue.get(), c.whenFalse.get()});
            return;
        }
        case NodeKind::IndexExpr: {
            auto& ix = static_cast<IndexExpr&>(e);
            scanAll({ix.object.get(), ix.index.get()});
            return;
        }
        case NodeKind::RangeExpr: {
            auto& r = static_cast<RangeExpr&>(e);
            scanAll({r.start.get(), r.end.get()});
            return;
        }
        default:
            return;
    }
}

// Forms with a single child expression.
void ClosureScanner::scanSingleChildExprs(Expr& e) {
    switch (e.kind) {
        case NodeKind::MemberExpr: {
            auto& m = static_cast<MemberExpr&>(e);
            scanMaybeExpr(m.object.get());
            return;
        }
        case NodeKind::UnaryExpr: {
            auto& u = static_cast<UnaryExpr&>(e);
            scanMaybeExpr(u.operand.get());
            return;
        }
        case NodeKind::SpreadElement: {
            auto& sp = static_cast<SpreadElement&>(e);
            scanMaybeExpr(sp.argument.get());
            return;
        }
        case NodeKind::AwaitExpr: {
            auto& a = static_cast<AwaitExpr&>(e);
            scanMaybeExpr(a.argument.get());
            return;
        }
        case NodeKind::WeakExpr: {
            auto& w = static_cast<WeakExpr&>(e);
            scanMaybeExpr(w.value.get());
            return;
        }
        default:
            return;
    }
}

// Forms that own a sequence of children: elements, properties, template parts
// and `new` arguments.
void ClosureScanner::scanContainerExprs(Expr& e) {
    switch (e.kind) {
        case NodeKind::ArrayLiteral: {
            auto& al = static_cast<ArrayLiteral&>(e);
            for (auto& el : al.elements) scanMaybeExpr(el.get());
            return;
        }
        case NodeKind::ObjectLiteral: {
            auto& ol = static_cast<ObjectLiteral&>(e);
            for (auto& p : ol.properties) {
                if (!p) continue;
                if (p->computed) scanMaybeExpr(p->key.get());
                scanMaybeExpr(p->value.get());
            }
            return;
        }
        case NodeKind::TemplateLiteral: {
            auto& tl = static_cast<TemplateLiteral&>(e);
            for (auto& ex : tl.expressions) scanMaybeExpr(ex.get());
            return;
        }
        case NodeKind::NewExpr: {
            auto& n = static_cast<NewExpr&>(e);
            scanMaybeExpr(n.callee.get());
            for (auto& a : n.arguments) scanMaybeExpr(a.get());
            return;
        }
        default:
            return;
    }
}

// =============================================================================
// Calls and write targets
// =============================================================================
// A call is read-only unless the callee is a member access whose property is a
// known in-place mutator (`.push`, `.add`, ...): calling one on captured state
// is CP3902.
void ClosureScanner::scanCall(CallExpr& c) {
    bool isParallelCall = false;
    if (c.callee && c.callee->kind == NodeKind::MemberExpr) {
        auto& m = static_cast<MemberExpr&>(*c.callee);
        isParallelCall = isParallelRoot(m.object.get());
        if (isMutatingMethod(m.property)) {
            Identifier* root = rootIdentifier(m.object.get());
            if (root && isCaptured(root->name)) {
                addCaptured(root->name, c.span);
            }
        }
    }
    scanMaybeExpr(c.callee.get());
    for (auto& a : c.arguments) {
        if (!a) continue;
        // A nested parallel.* closure is checked by its own call site with its
        // own induction variable; do not rescan it under this one.
        if (isParallelCall && a->kind == NodeKind::ArrowFunction) continue;
        scanExpr(*a);
    }
}

void ClosureScanner::scanTarget(Expr& target) {
    switch (target.kind) {
        case NodeKind::Identifier: {
            auto& id = static_cast<Identifier&>(target);
            if (isCaptured(id.name)) addCaptured(id.name, target.span);
            return;
        }
        case NodeKind::MemberExpr:
            scanMemberTarget(static_cast<MemberExpr&>(target));
            return;
        case NodeKind::IndexExpr:
            scanIndexTarget(static_cast<IndexExpr&>(target));
            return;
        default:
            scanExpr(target);
            return;
    }
}

// A field write through a captured root is an unsynchronized write (CP3902);
// the receiver is still walked so nested writes are found.
void ClosureScanner::scanMemberTarget(MemberExpr& m) {
    Identifier* root = rootIdentifier(m.object.get());
    if (root && isCaptured(root->name)) addCaptured(root->name, m.span);
    scanMaybeExpr(m.object.get());
}

// An indexed write through a captured root is CP3901 unless the index is
// parallel.for's own induction variable.
void ClosureScanner::scanIndexTarget(IndexExpr& ix) {
    Identifier* root = rootIdentifier(ix.object.get());
    if (root && isCaptured(root->name) && !throughInduction(ix)) {
        addIndex(ix.span);
    }
    scanAll({ix.object.get(), ix.index.get()});
}

}  // namespace parallel_detail
}  // namespace cp3
