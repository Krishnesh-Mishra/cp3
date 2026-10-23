// =============================================================================
// src/codegen/src/emit/worker_scan_stmt.cpp - the inline worker statement walk.
//
// OWNERSHIP: codegen module. The statement half of the inline worker pre-scan
// (D80): it walks declarations, control flow and try/switch bodies inside the
// entry arrow, declaring bindings into the shared scope stack and visiting
// every expression through emit/worker_scan.cpp's `walkExpr`. Split from
// worker_scan.cpp for the file budget (STYLE.md sec.2).
// =============================================================================
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "detail/worker/worker_scan_internal.h"

namespace cp3::detail {

namespace {

void walkVarDecl(const VariableDeclaration& vd, SiteScan& scan) {
    for (const auto& declPtr : vd.declarations) {
        if (!declPtr) continue;
        walkPattern(declPtr->id, scan);
        if (declPtr->init) walkExpr(*declPtr->init, scan);
    }
}

void walkFunction(const FunctionDeclaration& fd, SiteScan& scan) {
    if (!fd.name.empty() && scan.inInline) declare(fd.name, scan);
    ScopeGuard guard(scan);
    for (const auto& paramPtr : fd.params) {
        if (!paramPtr) continue;
        walkPattern(paramPtr->name, scan);
        if (paramPtr->defaultValue) walkExpr(*paramPtr->defaultValue, scan);
    }
    if (fd.body) walkStmt(*fd.body, scan);
}

// A for/for-of/for-in header: the init may be a declaration or an expression.
void walkForHeader(const NodePtr& left, const ExprPtr& condition,
                   const ExprPtr& update, SiteScan& scan) {
    if (left) {
        if (left->kind == NodeKind::VariableDeclaration) {
            walkVarDecl(static_cast<const VariableDeclaration&>(*left), scan);
        } else {
            walkExpr(static_cast<const Expr&>(*left), scan);
        }
    }
    if (condition) walkExpr(*condition, scan);
    if (update) walkExpr(*update, scan);
}

void walkFor(const ForStatement& f, SiteScan& scan) {
    ScopeGuard guard(scan);
    walkForHeader(f.init, f.condition, f.update, scan);
    if (f.body) walkStmt(*f.body, scan);
}

void walkForOf(const ForOfStatement& f, SiteScan& scan) {
    ScopeGuard guard(scan);
    walkForHeader(f.left, f.right, nullptr, scan);
    if (f.body) walkStmt(*f.body, scan);
}

void walkSwitch(const SwitchStatement& sw, SiteScan& scan) {
    if (sw.discriminant) walkExpr(*sw.discriminant, scan);
    ScopeGuard guard(scan);
    for (const auto& casePtr : sw.cases) {
        if (!casePtr) continue;
        if (casePtr->test) walkExpr(*casePtr->test, scan);
        for (const auto& child : casePtr->statements) {
            if (child) walkStmt(*child, scan);
        }
    }
}

void walkTry(const TryStatement& t, SiteScan& scan) {
    if (t.tryBlock) walkStmt(*t.tryBlock, scan);
    if (t.handler) {
        ScopeGuard guard(scan);
        if (t.handler->param) walkPattern(t.handler->param, scan);
        if (t.handler->body) walkStmt(*t.handler->body, scan);
    }
    if (t.finallyBlock) walkStmt(*t.finallyBlock, scan);
}

void walkWhile(const WhileStatement& w, SiteScan& scan) {
    if (w.condition) walkExpr(*w.condition, scan);
    if (w.body) walkStmt(*w.body, scan);
}

}  // namespace

void walkStmt(const Stmt& s, SiteScan& scan) {
    switch (s.kind) {
        case NodeKind::ExpressionStatement: {
            const auto& e = static_cast<const ExpressionStatement&>(s);
            if (e.expression) walkExpr(*e.expression, scan);
            return;
        }
        case NodeKind::BlockStatement: {
            ScopeGuard guard(scan);
            for (const auto& child : static_cast<const BlockStatement&>(s).body) {
                if (child) walkStmt(*child, scan);
            }
            return;
        }
        case NodeKind::VariableDeclaration:
            walkVarDecl(static_cast<const VariableDeclaration&>(s), scan);
            return;
        case NodeKind::FunctionDeclaration:
            walkFunction(static_cast<const FunctionDeclaration&>(s), scan);
            return;
        case NodeKind::IfStatement: {
            const auto& i = static_cast<const IfStatement&>(s);
            if (i.condition) walkExpr(*i.condition, scan);
            if (i.thenBranch) walkStmt(*i.thenBranch, scan);
            if (i.elseBranch) walkStmt(*i.elseBranch, scan);
            return;
        }
        case NodeKind::WhileStatement:
        case NodeKind::DoWhileStatement:
            walkWhile(static_cast<const WhileStatement&>(s), scan);
            return;
        case NodeKind::ForStatement:
            walkFor(static_cast<const ForStatement&>(s), scan);
            return;
        case NodeKind::ForOfStatement:
        case NodeKind::ForInStatement:
            walkForOf(static_cast<const ForOfStatement&>(s), scan);
            return;
        case NodeKind::SwitchStatement:
            walkSwitch(static_cast<const SwitchStatement&>(s), scan);
            return;
        case NodeKind::ReturnStatement: {
            const auto& r = static_cast<const ReturnStatement&>(s);
            if (r.argument) walkExpr(*r.argument, scan);
            return;
        }
        case NodeKind::ThrowStatement: {
            const auto& t = static_cast<const ThrowStatement&>(s);
            if (t.argument) walkExpr(*t.argument, scan);
            return;
        }
        case NodeKind::TryStatement:
            walkTry(static_cast<const TryStatement&>(s), scan);
            return;
        case NodeKind::LabeledStatement: {
            const auto& l = static_cast<const LabeledStatement&>(s);
            if (l.body) walkStmt(*l.body, scan);
            return;
        }
        case NodeKind::ExportDeclaration: {
            const auto& e = static_cast<const ExportDeclaration&>(s);
            if (e.declaration) walkStmt(*e.declaration, scan);
            if (e.defaultExpr) walkExpr(*e.defaultExpr, scan);
            return;
        }
        default:
            return;  // imports, enums, interfaces, classes, empty/break/continue
    }
}

}  // namespace cp3::detail
