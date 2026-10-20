// =============================================================================
// src/cli/src/toolchain/worker_sites.cpp - the `new Worker("<path>")` scan
// (D80, spec 13).
//
// The CLI must resolve the file-form sites before codegen so each worker unit
// gets its own generated TU. This unit walks the entry AST and collects every
// `new Worker(<string literal>)` site with the literal's span (the diagnostic
// location for a missing file). It is a pure syntax scan: the semantic checks
// (void directions, payload identity, handle misuse) live in codegen, which
// sees the resolved channel.
//
// A user declaration named `Worker` disables the builtin: `new Worker(...)` is
// then an ordinary constructor call, and codegen reports it as such. The scan
// only needs the top-level function set for that, matching codegen's rule.
//
// The walker is split by node family (`walkContainer` / `walkOperator` /
// `walkStmt` plus the statement helpers) to keep each function within the
// style budget; the two expression dispatchers handle disjoint node kinds, so
// every expression visits each at most once.
// =============================================================================
#include "detail/worker_module.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "cp3/ast.hpp"

namespace cp3::cli {

namespace {

// True when the unit declares a top-level function named `Worker` (an
// `export`-wrapped declaration included): the builtin is shadowed.
bool declaresWorkerFunction(const Stmt& s) {
    const Stmt* target = &s;
    if (s.kind == NodeKind::ExportDeclaration) {
        const auto& ed = static_cast<const ExportDeclaration&>(s);
        if (!ed.declaration) return false;
        target = ed.declaration.get();
    }
    if (target->kind != NodeKind::FunctionDeclaration) return false;
    return static_cast<const FunctionDeclaration&>(*target).name == "Worker";
}

struct PathScan {
    bool shadowed = false;
    std::vector<WorkerPathSite> sites;
};

void walkExpr(const Expr& e, PathScan& scan);
void walkStmt(const Stmt& s, PathScan& scan);

void walkList(const std::vector<ExprPtr>& list, PathScan& scan) {
    for (const auto& element : list) {
        if (element) walkExpr(*element, scan);
    }
}

void addSite(const NewExpr& n, PathScan& scan) {
    if (scan.shadowed || !n.callee || n.callee->kind != NodeKind::Identifier) return;
    if (static_cast<const Identifier&>(*n.callee).name != "Worker") return;
    if (n.arguments.size() != 1 || !n.arguments.front()) return;
    const Expr& arg = *n.arguments.front();
    if (arg.kind != NodeKind::StringLiteral) return;
    const auto& literal = static_cast<const StringLiteral&>(arg);
    scan.sites.push_back(WorkerPathSite{n.nodeId, literal.value, literal.span});
}

// Literal, container and arrow expressions.
void walkContainer(const Expr& e, PathScan& scan) {
    switch (e.kind) {
        case NodeKind::TemplateLiteral:
            walkList(static_cast<const TemplateLiteral&>(e).expressions, scan);
            return;
        case NodeKind::ArrayLiteral:
            walkList(static_cast<const ArrayLiteral&>(e).elements, scan);
            return;
        case NodeKind::ObjectLiteral: {
            const auto& o = static_cast<const ObjectLiteral&>(e);
            for (const auto& propPtr : o.properties) {
                if (!propPtr) continue;
                if (propPtr->computed && propPtr->key) walkExpr(*propPtr->key, scan);
                if (propPtr->value) {
                    walkExpr(*propPtr->value, scan);
                } else if (propPtr->shorthand && propPtr->key) {
                    walkExpr(*propPtr->key, scan);
                }
            }
            return;
        }
        case NodeKind::SpreadElement: {
            const auto& s = static_cast<const SpreadElement&>(e);
            if (s.argument) walkExpr(*s.argument, scan);
            return;
        }
        case NodeKind::WeakExpr: {
            const auto& w = static_cast<const WeakExpr&>(e);
            if (w.value) walkExpr(*w.value, scan);
            return;
        }
        case NodeKind::ArrowFunction: {
            const auto& a = static_cast<const ArrowFunction&>(e);
            for (const auto& p : a.params) {
                if (p && p->defaultValue) walkExpr(*p->defaultValue, scan);
            }
            if (!a.body) return;
            if (a.expressionBody) {
                walkExpr(*static_cast<const Expr*>(a.body.get()), scan);
            } else {
                walkStmt(*static_cast<const Stmt*>(a.body.get()), scan);
            }
            return;
        }
        default:
            return;
    }
}

// Operator, call and access expressions.
void walkOperator(const Expr& e, PathScan& scan) {
    switch (e.kind) {
        case NodeKind::MemberExpr: {
            const auto& m = static_cast<const MemberExpr&>(e);
            if (m.object) walkExpr(*m.object, scan);
            return;
        }
        case NodeKind::IndexExpr: {
            const auto& i = static_cast<const IndexExpr&>(e);
            if (i.object) walkExpr(*i.object, scan);
            if (i.index) walkExpr(*i.index, scan);
            return;
        }
        case NodeKind::CallExpr: {
            const auto& c = static_cast<const CallExpr&>(e);
            if (c.callee) walkExpr(*c.callee, scan);
            walkList(c.arguments, scan);
            return;
        }
        case NodeKind::NewExpr: {
            const auto& n = static_cast<const NewExpr&>(e);
            addSite(n, scan);
            if (n.callee) walkExpr(*n.callee, scan);
            walkList(n.arguments, scan);
            return;
        }
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(e);
            if (u.operand) walkExpr(*u.operand, scan);
            return;
        }
        case NodeKind::UpdateExpr: {
            const auto& u = static_cast<const UpdateExpr&>(e);
            if (u.operand) walkExpr(*u.operand, scan);
            return;
        }
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            if (b.left) walkExpr(*b.left, scan);
            if (b.right) walkExpr(*b.right, scan);
            return;
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(e);
            if (l.left) walkExpr(*l.left, scan);
            if (l.right) walkExpr(*l.right, scan);
            return;
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(e);
            if (c.condition) walkExpr(*c.condition, scan);
            if (c.whenTrue) walkExpr(*c.whenTrue, scan);
            if (c.whenFalse) walkExpr(*c.whenFalse, scan);
            return;
        }
        case NodeKind::AssignmentExpr: {
            const auto& a = static_cast<const AssignmentExpr&>(e);
            if (a.target) walkExpr(*a.target, scan);
            if (a.value) walkExpr(*a.value, scan);
            return;
        }
        case NodeKind::AwaitExpr: {
            const auto& a = static_cast<const AwaitExpr&>(e);
            if (a.argument) walkExpr(*a.argument, scan);
            return;
        }
        case NodeKind::RangeExpr: {
            const auto& r = static_cast<const RangeExpr&>(e);
            if (r.start) walkExpr(*r.start, scan);
            if (r.end) walkExpr(*r.end, scan);
            return;
        }
        default:
            return;
    }
}

void walkExpr(const Expr& e, PathScan& scan) {
    walkContainer(e, scan);
    walkOperator(e, scan);
}

void walkVarDecl(const VariableDeclaration& vd, PathScan& scan) {
    for (const auto& declPtr : vd.declarations) {
        if (declPtr && declPtr->init) walkExpr(*declPtr->init, scan);
    }
}

// A for/for-of/for-in header: the init may be a declaration or an expression.
void walkForHeader(const NodePtr& left, const ExprPtr& condition,
                   const ExprPtr& update, PathScan& scan) {
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

void walkSwitch(const SwitchStatement& sw, PathScan& scan) {
    if (sw.discriminant) walkExpr(*sw.discriminant, scan);
    for (const auto& casePtr : sw.cases) {
        if (!casePtr) continue;
        if (casePtr->test) walkExpr(*casePtr->test, scan);
        for (const auto& child : casePtr->statements) {
            if (child) walkStmt(*child, scan);
        }
    }
}

void walkTry(const TryStatement& t, PathScan& scan) {
    if (t.tryBlock) walkStmt(*t.tryBlock, scan);
    if (t.handler) {
        if (t.handler->body) walkStmt(*t.handler->body, scan);
    }
    if (t.finallyBlock) walkStmt(*t.finallyBlock, scan);
}

void walkWhile(const WhileStatement& w, PathScan& scan) {
    if (w.condition) walkExpr(*w.condition, scan);
    if (w.body) walkStmt(*w.body, scan);
}

void walkStmt(const Stmt& s, PathScan& scan) {
    switch (s.kind) {
        case NodeKind::ExpressionStatement: {
            const auto& e = static_cast<const ExpressionStatement&>(s);
            if (e.expression) walkExpr(*e.expression, scan);
            return;
        }
        case NodeKind::BlockStatement: {
            for (const auto& child : static_cast<const BlockStatement&>(s).body) {
                if (child) walkStmt(*child, scan);
            }
            return;
        }
        case NodeKind::VariableDeclaration:
            walkVarDecl(static_cast<const VariableDeclaration&>(s), scan);
            return;
        case NodeKind::FunctionDeclaration: {
            const auto& f = static_cast<const FunctionDeclaration&>(s);
            for (const auto& p : f.params) {
                if (p && p->defaultValue) walkExpr(*p->defaultValue, scan);
            }
            if (f.body) walkStmt(*f.body, scan);
            return;
        }
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
        case NodeKind::ForStatement: {
            const auto& f = static_cast<const ForStatement&>(s);
            walkForHeader(f.init, f.condition, f.update, scan);
            if (f.body) walkStmt(*f.body, scan);
            return;
        }
        case NodeKind::ForOfStatement:
        case NodeKind::ForInStatement: {
            const auto& f = static_cast<const ForOfStatement&>(s);
            walkForHeader(f.left, f.right, nullptr, scan);
            if (f.body) walkStmt(*f.body, scan);
            return;
        }
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
            return;  // imports/enums/interfaces/classes/empty/break/continue
    }
}

}  // namespace

std::vector<WorkerPathSite> collectWorkerPathSites(const Program& program) {
    PathScan scan;
    for (const auto& stmtPtr : program.body) {
        if (stmtPtr && declaresWorkerFunction(*stmtPtr)) scan.shadowed = true;
    }
    for (const auto& stmtPtr : program.body) {
        if (stmtPtr) walkStmt(*stmtPtr, scan);
    }
    return std::move(scan.sites);
}

}  // namespace cp3::cli
