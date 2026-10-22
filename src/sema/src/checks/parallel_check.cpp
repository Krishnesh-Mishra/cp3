// =============================================================================
// src/sema/src/checks/parallel_check.cpp - parallel.* capture safety: statements and
// the `Analyzer` call-site hook.
//
// Implements the statement half of the capture-safety rules of
// docs/language-spec §13 ("Capture safety, precisely:" - Rule 2 and the
// "Reassigning a captured let/var inside a parallel.* closure is a compile
// error too" paragraph):
//
//   * a write (reassignment, compound assignment, ++/--, field write, or an
//     in-place mutating method such as .push) to a variable captured from the
//     enclosing scope is CP3902 (kParallelCaptureReassign);
//   * an indexed write to captured state is CP3901 (kParallelSharedMutation)
//     unless the index is parallel.for's own induction variable, the one
//     provably disjoint write;
//   * reading captured state is always allowed.
//
// The scanner walks the closure body structurally (it does NOT re-run type
// inference) and is declared in parallel_check.h: statements are here,
// expressions and closure-local scope tracking in parallel_expr_check.cpp,
// and the `Analyzer` entry point at the bottom of this file. Diagnostics anchor
// at column 1 of the offending node's begin line (D54).
//
// The call-site hook also rejects a closure shape `parallel.*` cannot run
// (CP3912): an `async` closure has no event loop to drive its awaits (D71,
// M2-PLAN item 12), and a `parallel.run` closure takes no arguments, so a
// closure that declares a parameter - or carries type parameters nothing can
// infer - has no meaning there.
// =============================================================================
#include "detail/parallel_check.h"

#include <string>
#include <utility>

#include "cp3/diagnostic_codes.hpp"

#include "detail/analyzer.h"

namespace cp3 {
namespace parallel_detail {

// =============================================================================
// Closure entry + statement walk
// =============================================================================
void ClosureScanner::scan(ArrowFunction& arrow) {
    pushScope();
    for (auto& p : arrow.params) {
        if (p) declarePattern(p->name.get());
    }
    if (arrow.body) {
        if (arrow.expressionBody) {
            scanExpr(*static_cast<Expr*>(arrow.body.get()));
        } else {
            scanStmt(static_cast<Stmt&>(*arrow.body));
        }
    }
    popScope();
}

// Statement dispatch: block/labeled forms, declarations, expression statements
// and the `return`/`throw` jumps are handled here; the control-flow statements
// go through scanControlStmt, and each form keeps its focused helper below.
void ClosureScanner::scanStmt(Stmt& s) {
    switch (s.kind) {
        case NodeKind::BlockStatement:
            scanBlock(static_cast<BlockStatement&>(s));
            return;
        case NodeKind::ExpressionStatement:
            scanMaybeExpr(static_cast<ExpressionStatement&>(s).expression.get());
            return;
        case NodeKind::VariableDeclaration:
            scanVarDecl(static_cast<VariableDeclaration&>(s));
            return;
        case NodeKind::FunctionDeclaration: {
            auto& fn = static_cast<FunctionDeclaration&>(s);
            scanFunctionBody(fn.body.get(), fn.params);
            return;
        }
        case NodeKind::ReturnStatement:
            scanMaybeExpr(static_cast<ReturnStatement&>(s).argument.get());
            return;
        case NodeKind::ThrowStatement:
            scanMaybeExpr(static_cast<ThrowStatement&>(s).argument.get());
            return;
        case NodeKind::LabeledStatement: {
            auto& l = static_cast<LabeledStatement&>(s);
            if (l.body) scanStmt(*l.body);
            return;
        }
        case NodeKind::ExportDeclaration:
            scanExport(static_cast<ExportDeclaration&>(s));
            return;
        case NodeKind::IfStatement:
        case NodeKind::WhileStatement:
        case NodeKind::DoWhileStatement:
        case NodeKind::ForStatement:
        case NodeKind::ForOfStatement:
        case NodeKind::ForInStatement:
        case NodeKind::SwitchStatement:
        case NodeKind::TryStatement:
            scanControlStmt(s);
            return;
        default:
            return;
    }
}

// Control-flow statements that own nested statement bodies: branches, loops,
// `switch` and `try`.
void ClosureScanner::scanControlStmt(Stmt& s) {
    switch (s.kind) {
        case NodeKind::IfStatement:
            scanIf(static_cast<IfStatement&>(s));
            return;
        case NodeKind::WhileStatement:
            scanWhile(static_cast<WhileStatement&>(s));
            return;
        case NodeKind::DoWhileStatement:
            scanDoWhile(static_cast<DoWhileStatement&>(s));
            return;
        case NodeKind::ForStatement:
            scanFor(static_cast<ForStatement&>(s));
            return;
        case NodeKind::ForOfStatement: {
            auto& f = static_cast<ForOfStatement&>(s);
            scanForEach(f.left.get(), f.right.get(), f.body.get());
            return;
        }
        case NodeKind::ForInStatement: {
            auto& f = static_cast<ForInStatement&>(s);
            scanForEach(f.left.get(), f.right.get(), f.body.get());
            return;
        }
        case NodeKind::SwitchStatement:
            scanSwitch(static_cast<SwitchStatement&>(s));
            return;
        case NodeKind::TryStatement:
            scanTry(static_cast<TryStatement&>(s));
            return;
        default:
            return;
    }
}

void ClosureScanner::scanBlock(BlockStatement& b) {
    pushScope();
    for (auto& st : b.body) {
        if (st) scanStmt(*st);
    }
    popScope();
}

// =============================================================================
// Branches and loops
// =============================================================================
void ClosureScanner::scanIf(IfStatement& is) {
    scanMaybeExpr(is.condition.get());
    if (is.thenBranch) scanStmt(*is.thenBranch);
    if (is.elseBranch) scanStmt(*is.elseBranch);
}

void ClosureScanner::scanWhile(WhileStatement& w) {
    scanMaybeExpr(w.condition.get());
    if (w.body) scanStmt(*w.body);
}

void ClosureScanner::scanDoWhile(DoWhileStatement& d) {
    if (d.body) scanStmt(*d.body);
    scanMaybeExpr(d.condition.get());
}

void ClosureScanner::scanFor(ForStatement& f) {
    pushScope();
    if (f.init) scanForInit(*f.init);
    scanMaybeExpr(f.condition.get());
    scanMaybeExpr(f.update.get());
    if (f.body) scanStmt(*f.body);
    popScope();
}

// for-of / for-in: the right-hand iterable is evaluated in the enclosing scope;
// the loop binding and body live in a fresh one.
void ClosureScanner::scanForEach(Node* left, Expr* right, Stmt* body) {
    pushScope();
    scanMaybeExpr(right);
    if (left) {
        if (left->kind == NodeKind::VariableDeclaration) {
            scanVarDecl(static_cast<VariableDeclaration&>(*left));
        } else {
            declarePattern(left);
        }
    }
    if (body) scanStmt(*body);
    popScope();
}

// =============================================================================
// Declarations, switch, try, function bodies
// =============================================================================
void ClosureScanner::scanVarDecl(VariableDeclaration& vd) {
    for (auto& d : vd.declarations) {
        if (!d) continue;
        scanMaybeExpr(d->init.get());
        declarePattern(d->id.get());
    }
}

void ClosureScanner::scanForInit(Node& init) {
    if (init.kind == NodeKind::VariableDeclaration) {
        scanVarDecl(static_cast<VariableDeclaration&>(init));
    } else if (init.kind == NodeKind::ExpressionStatement) {
        auto& es = static_cast<ExpressionStatement&>(init);
        scanMaybeExpr(es.expression.get());
    }
}

void ClosureScanner::scanSwitch(SwitchStatement& sw) {
    scanMaybeExpr(sw.discriminant.get());
    for (auto& c : sw.cases) {
        if (!c) continue;
        pushScope();
        scanMaybeExpr(c->test.get());
        for (auto& st : c->statements) {
            if (st) scanStmt(*st);
        }
        popScope();
    }
}

void ClosureScanner::scanTry(TryStatement& ts) {
    if (ts.tryBlock) scanStmt(*ts.tryBlock);
    if (ts.handler) {
        pushScope();
        declarePattern(ts.handler->param.get());
        if (ts.handler->body) scanStmt(*ts.handler->body);
        popScope();
    }
    if (ts.finallyBlock) scanStmt(*ts.finallyBlock);
}

void ClosureScanner::scanFunctionBody(Stmt* body, ParamList& params) {
    pushScope();
    for (auto& p : params) {
        if (p) declarePattern(p->name.get());
    }
    if (body) scanStmt(*body);
    popScope();
}

void ClosureScanner::scanExport(ExportDeclaration& e) {
    scanMaybeExpr(e.defaultExpr.get());
    if (e.declaration) scanStmt(*e.declaration);
}

}  // namespace parallel_detail

// =============================================================================
// Analyzer entry point
// =============================================================================
void Analyzer::checkParallelCall(CallExpr& call) {
    if (!call.callee || call.callee->kind != NodeKind::MemberExpr) return;
    auto& m = static_cast<MemberExpr&>(*call.callee);
    if (!parallel_detail::isParallelRoot(m.object.get())) return;
    if (call.nodeId != 0 && !parallelChecked_.insert(call.nodeId).second) return;

    const bool isFor = m.property == "for";
    const bool isRun = m.property == "run";
    for (auto& a : call.arguments) {
        if (!a || a->kind != NodeKind::ArrowFunction) continue;
        auto& arrow = static_cast<ArrowFunction&>(*a);
        // D71 and M2-PLAN item 12: a `parallel.*` closure runs on a pool
        // thread that has no event loop, and `parallel.*` is synchronous by
        // contract (spec §13 Rule 1). An `async` closure would leave its
        // awaits with nothing to drive them, so it is a clean compile-time
        // diagnostic - never a silently broken program.
        if (arrow.isAsync) {
            report(diagcode::kParallelClosureShape, arrow.span,
                   "a parallel.* closure cannot be async: parallel.* is "
                   "synchronous and its pool threads have no event loop, so an "
                   "await inside the closure would never be driven");
        } else if (isRun && !arrow.typeParams.empty()) {
            // `parallel.run(f1, ..., fN)` calls each closure with no
            // arguments, so a generic closure has no call site to infer its
            // type arguments from.
            report(diagcode::kParallelClosureShape, arrow.span,
                   "a parallel.run() closure cannot be generic: the call site "
                   "has no argument to infer its type parameters from");
        } else if (isRun && !arrow.params.empty()) {
            report(diagcode::kParallelClosureShape, arrow.span,
                   "a parallel.run() closure takes no parameters: the call "
                   "runs each closure with no arguments");
        }
        std::string induction;
        if (isFor && !arrow.params.empty() && arrow.params.front()) {
            induction = parallel_detail::boundName(arrow.params.front()->name.get());
        }
        parallel_detail::ClosureScanner scanner(
            induction, [this](const std::string& name) {
                Binding b;
                return lookup(name, b);
            });
        scanner.scan(arrow);
        for (parallel_detail::Finding& f : scanner.take()) {
            report(f.code, f.span, std::move(f.message));
        }
    }
}

}  // namespace cp3
