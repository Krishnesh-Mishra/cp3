// =============================================================================
// src/codegen/src/emit/worker_scan.cpp - the `worker(...)` argument pre-scan.
//
// `collectWorkerObjects` (declared in detail/generator.h) walks the entry unit
// before emission, only when a canonical worker import resolved, and marks the
// object literals that are handed to `worker(...)`: directly, or through
// exactly one binding that is never reassigned and declared exactly once with
// the literal as its initializer. The marked literals lower to a `std::tuple`
// instead of reporting CP3002 (src/emit/worker.cpp). Everything the walk cannot
// prove marks nothing, which keeps the call CP3002: a skip, never a wrong deep
// copy.
// =============================================================================
#include "detail/generator.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cp3 {

namespace {

// What `collectWorkerObjects` gathers in one walk.
struct WorkerScan {
    std::unordered_set<std::string> argNames;      // names passed to worker(...)
    std::unordered_set<std::string> assigned;      // assignment/update targets
    std::unordered_map<std::string, int> declares; // declarator-id counts
    std::unordered_map<std::string, std::uint32_t> literalInit;  // name -> nodeId
    std::unordered_set<std::uint32_t> inlineLiterals;
};

void scanWorkerExpr(const Expr& e, WorkerScan& scan);
void scanWorkerStmt(const Stmt& s, WorkerScan& scan);

// Records one `worker(...)` argument: a binding to inspect, or an inline
// object literal handed over directly.
void scanWorkerArg(const Expr& arg, WorkerScan& scan) {
    if (arg.kind == NodeKind::Identifier) {
        scan.argNames.insert(static_cast<const Identifier&>(arg).name);
    } else if (arg.kind == NodeKind::ObjectLiteral) {
        scan.inlineLiterals.insert(arg.nodeId);
    }
}

// The expression-carrying statements (expressions, declarations, returns,
// throws, function bodies, exports). Returns false for control flow, which
// `scanWorkerStmt` dispatches from itself.
bool scanWorkerLeafStmt(const Stmt& s, WorkerScan& scan) {
    switch (s.kind) {
        case NodeKind::ExpressionStatement: {
            const auto& es = static_cast<const ExpressionStatement&>(s);
            if (es.expression) scanWorkerExpr(*es.expression, scan);
            return true;
        }
        case NodeKind::VariableDeclaration: {
            const auto& vd = static_cast<const VariableDeclaration&>(s);
            for (const auto& declPtr : vd.declarations) {
                const VariableDeclarator* d = declPtr.get();
                if (!d || !d->id || d->id->kind != NodeKind::Identifier) continue;
                const std::string& name =
                    static_cast<const Identifier&>(*d->id).name;
                ++scan.declares[name];
                if (!d->init) continue;
                if (d->init->kind == NodeKind::ObjectLiteral) {
                    scan.literalInit[name] = d->init->nodeId;
                }
                scanWorkerExpr(*d->init, scan);
            }
            return true;
        }
        case NodeKind::ReturnStatement: {
            const auto& r = static_cast<const ReturnStatement&>(s);
            if (r.argument) scanWorkerExpr(*r.argument, scan);
            return true;
        }
        case NodeKind::ThrowStatement: {
            const auto& t = static_cast<const ThrowStatement&>(s);
            if (t.argument) scanWorkerExpr(*t.argument, scan);
            return true;
        }
        case NodeKind::FunctionDeclaration: {
            const auto& fd = static_cast<const FunctionDeclaration&>(s);
            if (fd.body) scanWorkerStmt(*fd.body, scan);
            return true;
        }
        case NodeKind::ExportDeclaration: {
            const auto& ed = static_cast<const ExportDeclaration&>(s);
            if (ed.declaration) scanWorkerStmt(*ed.declaration, scan);
            if (ed.defaultExpr) scanWorkerExpr(*ed.defaultExpr, scan);
            return true;
        }
        default:
            return false;
    }
}

// The nesting statement kinds (try/switch/labeled bodies); `scanWorkerStmt`
// falls through to them for anything it does not handle itself.
void scanWorkerStmtNested(const Stmt& s, WorkerScan& scan) {
    switch (s.kind) {
        case NodeKind::TryStatement: {
            const auto& t = static_cast<const TryStatement&>(s);
            if (t.tryBlock) scanWorkerStmt(*t.tryBlock, scan);
            if (t.handler && t.handler->body) scanWorkerStmt(*t.handler->body, scan);
            if (t.finallyBlock) scanWorkerStmt(*t.finallyBlock, scan);
            return;
        }
        case NodeKind::SwitchStatement: {
            const auto& sw = static_cast<const SwitchStatement&>(s);
            if (sw.discriminant) scanWorkerExpr(*sw.discriminant, scan);
            for (const auto& casePtr : sw.cases) {
                if (!casePtr) continue;
                if (casePtr->test) scanWorkerExpr(*casePtr->test, scan);
                for (const auto& child : casePtr->statements) {
                    if (child) scanWorkerStmt(*child, scan);
                }
            }
            return;
        }
        case NodeKind::LabeledStatement: {
            const auto& ls = static_cast<const LabeledStatement&>(s);
            if (ls.body) scanWorkerStmt(*ls.body, scan);
            return;
        }
        default:
            return;  // everything else cannot mention worker(...) usefully
    }
}

void scanWorkerStmt(const Stmt& s, WorkerScan& scan) {
    if (scanWorkerLeafStmt(s, scan)) return;
    switch (s.kind) {
        case NodeKind::BlockStatement: {
            for (const auto& child : static_cast<const BlockStatement&>(s).body) {
                if (child) scanWorkerStmt(*child, scan);
            }
            return;
        }
        case NodeKind::IfStatement: {
            const auto& is = static_cast<const IfStatement&>(s);
            if (is.condition) scanWorkerExpr(*is.condition, scan);
            if (is.thenBranch) scanWorkerStmt(*is.thenBranch, scan);
            if (is.elseBranch) scanWorkerStmt(*is.elseBranch, scan);
            return;
        }
        case NodeKind::WhileStatement:
        case NodeKind::DoWhileStatement: {
            const auto& w = static_cast<const WhileStatement&>(s);
            if (w.condition) scanWorkerExpr(*w.condition, scan);
            if (w.body) scanWorkerStmt(*w.body, scan);
            return;
        }
        case NodeKind::ForStatement: {
            const auto& f = static_cast<const ForStatement&>(s);
            if (f.init) {
                if (f.init->kind == NodeKind::VariableDeclaration) {
                    scanWorkerStmt(static_cast<const Stmt&>(*f.init), scan);
                } else {
                    scanWorkerExpr(static_cast<const Expr&>(*f.init), scan);
                }
            }
            if (f.condition) scanWorkerExpr(*f.condition, scan);
            if (f.update) scanWorkerExpr(*f.update, scan);
            if (f.body) scanWorkerStmt(*f.body, scan);
            return;
        }
        case NodeKind::ForOfStatement:
        case NodeKind::ForInStatement: {
            const auto& f = static_cast<const ForOfStatement&>(s);
            if (f.left && f.left->kind == NodeKind::VariableDeclaration) {
                scanWorkerStmt(static_cast<const Stmt&>(*f.left), scan);
            }
            if (f.right) scanWorkerExpr(*f.right, scan);
            if (f.body) scanWorkerStmt(*f.body, scan);
            return;
        }
        default:
            scanWorkerStmtNested(s, scan);
            return;
    }
}

// The list-carrying expressions (constructor args, template parts, array
// elements, property values, arrow bodies).
void scanWorkerExprList(const Expr& e, WorkerScan& scan) {
    switch (e.kind) {
        case NodeKind::NewExpr: {
            const auto& n = static_cast<const NewExpr&>(e);
            for (const auto& arg : n.arguments) {
                if (arg) scanWorkerExpr(*arg, scan);
            }
            return;
        }
        case NodeKind::TemplateLiteral: {
            const auto& tl = static_cast<const TemplateLiteral&>(e);
            for (const auto& part : tl.expressions) {
                if (part) scanWorkerExpr(*part, scan);
            }
            return;
        }
        case NodeKind::ArrayLiteral: {
            const auto& al = static_cast<const ArrayLiteral&>(e);
            for (const auto& element : al.elements) {
                if (element) scanWorkerExpr(*element, scan);
            }
            return;
        }
        case NodeKind::ObjectLiteral: {
            const auto& ol = static_cast<const ObjectLiteral&>(e);
            for (const auto& propertyPtr : ol.properties) {
                const ObjectProperty* p = propertyPtr.get();
                if (p && p->value) scanWorkerExpr(*p->value, scan);
            }
            return;
        }
        case NodeKind::ArrowFunction: {
            const auto& a = static_cast<const ArrowFunction&>(e);
            if (!a.body) return;
            if (a.expressionBody) {
                scanWorkerExpr(*static_cast<const Expr*>(a.body.get()), scan);
            } else {
                scanWorkerStmt(*static_cast<const Stmt*>(a.body.get()), scan);
            }
            return;
        }
        default:
            return;
    }
}

// The simple one/two-child expressions.
void scanWorkerExprPair(const Expr& e, WorkerScan& scan) {
    switch (e.kind) {
        case NodeKind::UnaryExpr: {
            const auto& u = static_cast<const UnaryExpr&>(e);
            if (u.operand) scanWorkerExpr(*u.operand, scan);
            return;
        }
        case NodeKind::AwaitExpr: {
            const auto& a = static_cast<const AwaitExpr&>(e);
            if (a.argument) scanWorkerExpr(*a.argument, scan);
            return;
        }
        case NodeKind::SpreadElement: {
            const auto& sp = static_cast<const SpreadElement&>(e);
            if (sp.argument) scanWorkerExpr(*sp.argument, scan);
            return;
        }
        case NodeKind::BinaryExpr: {
            const auto& b = static_cast<const BinaryExpr&>(e);
            if (b.left) scanWorkerExpr(*b.left, scan);
            if (b.right) scanWorkerExpr(*b.right, scan);
            return;
        }
        case NodeKind::LogicalExpr: {
            const auto& l = static_cast<const LogicalExpr&>(e);
            if (l.left) scanWorkerExpr(*l.left, scan);
            if (l.right) scanWorkerExpr(*l.right, scan);
            return;
        }
        case NodeKind::ConditionalExpr: {
            const auto& c = static_cast<const ConditionalExpr&>(e);
            if (c.condition) scanWorkerExpr(*c.condition, scan);
            if (c.whenTrue) scanWorkerExpr(*c.whenTrue, scan);
            if (c.whenFalse) scanWorkerExpr(*c.whenFalse, scan);
            return;
        }
        case NodeKind::MemberExpr: {
            const auto& m = static_cast<const MemberExpr&>(e);
            if (m.object) scanWorkerExpr(*m.object, scan);
            return;
        }
        case NodeKind::IndexExpr: {
            const auto& ix = static_cast<const IndexExpr&>(e);
            if (ix.object) scanWorkerExpr(*ix.object, scan);
            if (ix.index) scanWorkerExpr(*ix.index, scan);
            return;
        }
        case NodeKind::RangeExpr: {
            const auto& r = static_cast<const RangeExpr&>(e);
            if (r.start) scanWorkerExpr(*r.start, scan);
            if (r.end) scanWorkerExpr(*r.end, scan);
            return;
        }
        default:
            return;  // identifiers/literals: nothing to record
    }
}

// The walk for everything except the recording cases of `scanWorkerExpr`.
void scanWorkerExprPlain(const Expr& e, WorkerScan& scan) {
    switch (e.kind) {
        case NodeKind::NewExpr:
        case NodeKind::TemplateLiteral:
        case NodeKind::ArrayLiteral:
        case NodeKind::ObjectLiteral:
        case NodeKind::ArrowFunction:
            scanWorkerExprList(e, scan);
            return;
        default:
            scanWorkerExprPair(e, scan);
            return;
    }
}

void scanWorkerExpr(const Expr& e, WorkerScan& scan) {
    switch (e.kind) {
        case NodeKind::CallExpr: {
            const auto& c = static_cast<const CallExpr&>(e);
            if (c.callee && c.callee->kind == NodeKind::Identifier &&
                static_cast<const Identifier&>(*c.callee).name == "worker") {
                for (const auto& arg : c.arguments) {
                    if (arg) scanWorkerArg(*arg, scan);
                }
            }
            if (c.callee) scanWorkerExpr(*c.callee, scan);
            for (const auto& arg : c.arguments) {
                if (arg) scanWorkerExpr(*arg, scan);
            }
            return;
        }
        case NodeKind::AssignmentExpr: {
            const auto& a = static_cast<const AssignmentExpr&>(e);
            if (a.target && a.target->kind == NodeKind::Identifier) {
                scan.assigned.insert(
                    static_cast<const Identifier&>(*a.target).name);
            }
            if (a.target) scanWorkerExpr(*a.target, scan);
            if (a.value) scanWorkerExpr(*a.value, scan);
            return;
        }
        case NodeKind::UpdateExpr: {
            const auto& u = static_cast<const UpdateExpr&>(e);
            if (u.operand && u.operand->kind == NodeKind::Identifier) {
                scan.assigned.insert(
                    static_cast<const Identifier&>(*u.operand).name);
            }
            if (u.operand) scanWorkerExpr(*u.operand, scan);
            return;
        }
        default:
            scanWorkerExprPlain(e, scan);
            return;
    }
}

}  // namespace

// -----------------------------------------------------------------------------
// The pre-scan
// -----------------------------------------------------------------------------

void Generator::collectWorkerObjects(const Program& program) {
    if (!options_.worker.has_value()) return;
    WorkerScan scan;
    for (const auto& stmtPtr : program.body) {
        if (stmtPtr) scanWorkerStmt(*stmtPtr, scan);
    }
    for (const std::string& name : scan.argNames) {
        if (scan.assigned.count(name) != 0) continue;
        auto count = scan.declares.find(name);
        if (count == scan.declares.end() || count->second != 1) continue;
        auto init = scan.literalInit.find(name);
        if (init == scan.literalInit.end()) continue;
        workerLiterals_.insert(init->second);
    }
    workerLiterals_.insert(scan.inlineLiterals.begin(), scan.inlineLiterals.end());
}

}  // namespace cp3
