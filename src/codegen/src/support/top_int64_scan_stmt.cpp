// =============================================================================
// src/codegen/src/support/top_int64_scan_stmt.cpp - the statement half of the
// whole-unit walk behind the entry-body int64 proof.
//
// The walk records every binding declaration, every reference, every write and
// every `while` loop of the unit. It is deliberately exhaustive: a construct
// it does not model turns `complete` false, and the proof then rejects every
// candidate in the unit (complete-or-nothing, like int_reads.cpp).
//
// The expression half lives in support/top_int64_scan_expr.cpp.
// =============================================================================
#include "detail/top_int64_scan.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cp3 {
namespace detail {

namespace {

using Scan = TopInt64Scan;

// A module declaration: the binding names and the initializer.
void scanVarDecl(const VariableDeclaration& vd, Scan& s) {
    for (const auto& d : vd.declarations) {
        if (!d) {
            s.complete = false;
            continue;
        }
        scanTopInt64Name(d->id.get(), s);
        if (d->init) scanTopInt64Expr(*d->init, s);
    }
}

// A declared function: its name, its parameters and its body (a deferred
// frame, so its writes run at a later call).
void scanFuncDecl(const FunctionDeclaration& fd, Scan& s) {
    if (!fd.name.empty()) {
        ++s.bindings[fd.name];
        s.idents.insert(fd.name);
    }
    const bool saved = s.inFrame;
    s.inFrame = true;
    scanTopInt64Params(fd.params, s);
    if (fd.body) scanTopInt64Stmt(*fd.body, s);
    s.inFrame = saved;
}

// An import binds names too: they count for the duplicate-binding proof and
// must never collide with a generated int64 alias.
void scanImportStmt(const ImportDeclaration& imp, Scan& s) {
    const std::string* names[] = {&imp.defaultBinding, &imp.namespaceBinding,
                                  &imp.alias};
    for (const std::string* name : names) {
        if (name->empty()) continue;
        ++s.bindings[*name];
        s.idents.insert(*name);
    }
    for (const auto& spec : imp.specifiers) {
        if (!spec || spec->local.empty()) continue;
        ++s.bindings[spec->local];
        s.idents.insert(spec->local);
    }
}

// The `while` loop: the condition, the body, and the loop's own place in the
// `continue` attribution.
void scanWhileStmt(const WhileStatement& ws, Scan& s) {
    s.whiles.push_back(&ws);
    if (ws.condition) scanTopInt64Expr(*ws.condition, s);
    s.whileStack.push_back(&ws);
    if (ws.body) scanTopInt64Stmt(*ws.body, s);
    s.whileStack.pop_back();
}

// The `for` loop: the header and the body.
void scanForStmt(const ForStatement& f, Scan& s) {
    if (f.init) {
        if (f.init->kind == NodeKind::VariableDeclaration) {
            scanTopInt64Stmt(static_cast<const Stmt&>(*f.init), s);
        } else if (auto* initExpr = dynamic_cast<const Expr*>(f.init.get())) {
            scanTopInt64Expr(*initExpr, s);
        } else {
            s.complete = false;
        }
    }
    if (f.condition) scanTopInt64Expr(*f.condition, s);
    if (f.update) scanTopInt64Expr(*f.update, s);
    if (f.body) scanTopInt64Stmt(*f.body, s);
}

// The `for...of` loop: the per-iteration binding, the source, the body.
void scanForOfStmt(const ForOfStatement& fo, Scan& s) {
    if (fo.left && fo.left->kind == NodeKind::VariableDeclaration) {
        scanTopInt64Stmt(static_cast<const Stmt&>(*fo.left), s);
    } else {
        s.complete = false;  // an existing binding is written
    }
    if (fo.right) scanTopInt64Expr(*fo.right, s);
    if (fo.body) scanTopInt64Stmt(*fo.body, s);
}

// The `try` statement: the guarded block, the catch binding and the finally
// block.
void scanTryStmt(const TryStatement& ts, Scan& s) {
    if (ts.tryBlock) scanTopInt64Stmt(*ts.tryBlock, s);
    if (ts.handler) {
        scanTopInt64Name(ts.handler->param.get(), s);
        if (ts.handler->body) scanTopInt64Stmt(*ts.handler->body, s);
    }
    if (ts.finallyBlock) scanTopInt64Stmt(*ts.finallyBlock, s);
}

// The `switch` statement: the discriminant and the case bodies.
void scanSwitchStmt(const SwitchStatement& sw, Scan& s) {
    if (sw.discriminant) scanTopInt64Expr(*sw.discriminant, s);
    for (const auto& c : sw.cases) {
        if (!c) {
            s.complete = false;
            continue;
        }
        if (c->test) scanTopInt64Expr(*c->test, s);
        for (const auto& child : c->statements) {
            if (child) scanTopInt64Stmt(*child, s);
        }
    }
}

// The `if` statement: the condition and the two branches.
void scanIfStmt(const IfStatement& is, Scan& s) {
    if (is.condition) scanTopInt64Expr(*is.condition, s);
    if (is.thenBranch) scanTopInt64Stmt(*is.thenBranch, s);
    if (is.elseBranch) scanTopInt64Stmt(*is.elseBranch, s);
}

// The statements that carry no expression of their own.
void scanSimpleStmt(const Stmt& st, Scan& s) {
    switch (st.kind) {
        case NodeKind::ExpressionStatement: {
            const auto& es = static_cast<const ExpressionStatement&>(st);
            if (es.expression) scanTopInt64Expr(*es.expression, s);
            return;
        }
        case NodeKind::ReturnStatement: {
            const auto& r = static_cast<const ReturnStatement&>(st);
            if (r.argument) scanTopInt64Expr(*r.argument, s);
            return;
        }
        case NodeKind::ThrowStatement: {
            const auto& t = static_cast<const ThrowStatement&>(st);
            if (t.argument) scanTopInt64Expr(*t.argument, s);
            return;
        }
        case NodeKind::LabeledStatement: {
            const auto& l = static_cast<const LabeledStatement&>(st);
            if (l.body) scanTopInt64Stmt(*l.body, s);
            return;
        }
        case NodeKind::ExportDeclaration: {
            const auto& ed = static_cast<const ExportDeclaration&>(st);
            if (ed.declaration) scanTopInt64Stmt(*ed.declaration, s);
            if (ed.defaultExpr) scanTopInt64Expr(*ed.defaultExpr, s);
            return;
        }
        default:
            // A statement kind the walk does not model.
            s.complete = false;
            return;
    }
}

}  // namespace

void scanTopInt64Stmt(const Stmt& st, TopInt64Scan& s) {
    switch (st.kind) {
        case NodeKind::EmptyStatement:
        case NodeKind::BreakStatement:
        case NodeKind::InterfaceDeclaration:
        case NodeKind::TypeAliasDeclaration:
        case NodeKind::EnumDeclaration:
            return;
        case NodeKind::ImportDeclaration:
            scanImportStmt(static_cast<const ImportDeclaration&>(st), s);
            return;
        case NodeKind::ContinueStatement:
            // A continue (labelled or not) is conservatively attributed to
            // every enclosing while: the `for` rewrite would run the header
            // step that the original body's `continue` skipped.
            for (const WhileStatement* ws : s.whileStack) {
                s.continued.insert(ws);
            }
            return;
        case NodeKind::VariableDeclaration:
            scanVarDecl(static_cast<const VariableDeclaration&>(st), s);
            return;
        case NodeKind::FunctionDeclaration:
            // A declaration (wherever it appears) is a reader outside the
            // entry function: `moduleUsesBeforeDecl` uses `complete` to force
            // the namespace-scope lowering. The body is still scanned so its
            // references and writes reach the whole-unit facts.
            scanFuncDecl(static_cast<const FunctionDeclaration&>(st), s);
            s.complete = false;
            return;
        case NodeKind::WhileStatement:
            scanWhileStmt(static_cast<const WhileStatement&>(st), s);
            return;
        case NodeKind::DoWhileStatement: {
            const auto& dw = static_cast<const DoWhileStatement&>(st);
            if (dw.body) scanTopInt64Stmt(*dw.body, s);
            if (dw.condition) scanTopInt64Expr(*dw.condition, s);
            return;
        }
        case NodeKind::ForStatement:
            scanForStmt(static_cast<const ForStatement&>(st), s);
            return;
        case NodeKind::ForOfStatement:
            scanForOfStmt(static_cast<const ForOfStatement&>(st), s);
            return;
        case NodeKind::ForInStatement:
            s.complete = false;  // unmodelled write
            return;
        case NodeKind::TryStatement:
            scanTryStmt(static_cast<const TryStatement&>(st), s);
            return;
        case NodeKind::ClassDeclaration:
            s.complete = false;
            return;
        case NodeKind::BlockStatement: {
            for (const auto& child : static_cast<const BlockStatement&>(st).body) {
                if (child) scanTopInt64Stmt(*child, s);
            }
            return;
        }
        case NodeKind::IfStatement:
            scanIfStmt(static_cast<const IfStatement&>(st), s);
            return;
        case NodeKind::SwitchStatement:
            scanSwitchStmt(static_cast<const SwitchStatement&>(st), s);
            return;
        default:
            scanSimpleStmt(st, s);
            return;
    }
}

void scanTopInt64Unit(const Program& program, TopInt64Scan& scan) {
    for (const auto& stmt : program.body) {
        if (stmt) scanTopInt64Stmt(*stmt, scan);
    }
}

}  // namespace detail
}  // namespace cp3
