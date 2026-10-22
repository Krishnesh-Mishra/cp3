// =============================================================================
// src/codegen/src/emit/worker_scan.cpp - the inline worker pre-scan (D80).
//
// The inline form `new Worker((thread: Thread<In, Out>) => { ... })` compiles
// through the same worker-unit pipeline as the file form: the arrow becomes one
// entry function of the entry TU, so it must see only what a worker unit sees.
// The pre-scan
//
//   * resolves each inline arrow's `Thread<In, Out>` annotation through the
//     sema type-node side table (missing/incorrect -> CP3905);
//   * walks the arrow body with a scope stack and reports any reference to an
//     enclosing *variable binding* - a local of the enclosing function or a
//     module variable of the entry unit - as CP3909. Top-level functions,
//     imports, builtins and types stay visible: they are code, not state, and
//     the generated entry function can call them;
//   * appends the valid sites for the emitter (src/emit/worker.cpp).
//
// This unit owns the scan state, the binding patterns and the expression
// visits; the statement walk lives in emit/worker_scan_stmt.cpp (file budget,
// STYLE.md sec.2). The expression visit is split by node family so each
// function stays within the budget.
// =============================================================================
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"
#include "detail/worker.h"
#include "detail/worker_scan_internal.h"

namespace cp3::detail {

bool isLocal(const std::string& name, const SiteScan& scan) {
    for (auto it = scan.scopes.rbegin(); it != scan.scopes.rend(); ++it) {
        if (it->count(name) != 0) return true;
    }
    return false;
}

void declare(const std::string& name, SiteScan& scan) {
    if (!scan.scopes.empty()) scan.scopes.back().insert(name);
}

void reportOnce(const Node& node, const std::string& code,
                const std::string& message, SiteScan& scan) {
    if (node.nodeId != 0 && !scan.reported.insert(node.nodeId).second) return;
    scan.sink.error(code, node.span, message);
}

namespace {

// One binding pattern's defaults (the right-hand sides of `x = e`).
void walkPatternDefaults(const Node& node, SiteScan& scan) {
    switch (node.kind) {
        case NodeKind::ObjectPatternProperty: {
            const auto& p = static_cast<const ObjectPatternProperty&>(node);
            if (p.computed && p.key) walkExpr(*p.key, scan);
            return;
        }
        case NodeKind::AssignmentPattern: {
            const auto& p = static_cast<const AssignmentPattern&>(node);
            if (p.right) walkExpr(*p.right, scan);
            return;
        }
        default:
            return;
    }
}

void walkPatternIdentifier(const NodePtr& node, SiteScan& scan) {
    if (!node) return;
    if (node->kind == NodeKind::Identifier) {
        declare(static_cast<const Identifier&>(*node).name, scan);
        return;
    }
    walkPattern(node, scan);
}

}  // namespace

void walkPattern(const NodePtr& node, SiteScan& scan) {
    if (!node) return;
    switch (node->kind) {
        case NodeKind::ArrayPattern: {
            const auto& p = static_cast<const ArrayPattern&>(*node);
            for (const auto& element : p.elements) walkPatternIdentifier(element, scan);
            walkPatternIdentifier(p.rest, scan);
            return;
        }
        case NodeKind::ObjectPattern: {
            const auto& p = static_cast<const ObjectPattern&>(*node);
            for (const auto& propPtr : p.properties) {
                if (!propPtr) continue;
                walkPatternDefaults(*propPtr, scan);
                if (!propPtr->computed) walkPatternIdentifier(propPtr->value, scan);
            }
            walkPatternIdentifier(p.rest, scan);
            return;
        }
        case NodeKind::RestElement:
            walkPatternIdentifier(static_cast<const RestElement&>(*node).argument, scan);
            return;
        case NodeKind::AssignmentPattern: {
            const auto& p = static_cast<const AssignmentPattern&>(*node);
            walkPatternIdentifier(p.left, scan);
            walkPatternDefaults(*node, scan);
            return;
        }
        case NodeKind::Identifier:
            declare(static_cast<const Identifier&>(*node).name, scan);
            return;
        default:
            return;
    }
}

namespace {

// An arrow function: its own scope, its parameters local. A worker entry arrow
// (`workerEntry`) additionally switches the capture rule on for its body; a
// nested arrow inherits the enclosing context.
void walkArrow(const ArrowFunction& arrow, SiteScan& scan, bool workerEntry) {
    ScopeGuard guard(scan);
    for (const auto& paramPtr : arrow.params) {
        if (!paramPtr) continue;
        walkPattern(paramPtr->name, scan);
    }
    for (const auto& paramPtr : arrow.params) {
        if (paramPtr && paramPtr->defaultValue) walkExpr(*paramPtr->defaultValue, scan);
    }
    const bool saved = scan.inInline;
    if (workerEntry) scan.inInline = true;
    if (arrow.body && arrow.expressionBody) {
        walkExpr(*static_cast<const Expr*>(arrow.body.get()), scan);
    } else if (arrow.body) {
        walkStmt(*static_cast<const Stmt*>(arrow.body.get()), scan);
    }
    scan.inInline = saved;
}

// A `new Worker(...)` expression: an inline arrow becomes one site (or a
// CP3905/CP3002 report), and its body is always walked for captures.
void walkWorkerNew(const NewExpr& n, SiteScan& scan) {
    const Expr* arg = n.arguments.size() == 1 ? n.arguments.front().get() : nullptr;
    if (arg == nullptr || arg->kind != NodeKind::ArrowFunction) {
        if (n.callee) walkExpr(*n.callee, scan);
        for (const auto& a : n.arguments) {
            if (a) walkExpr(*a, scan);
        }
        return;
    }
    const auto& arrow = static_cast<const ArrowFunction&>(*arg);
    const std::optional<WorkerChannel> channel = channelOfArrow(arrow, scan.sema);
    const Parameter* param = arrow.params.size() == 1 ? arrow.params[0].get() : nullptr;
    const bool named = param != nullptr && param->name &&
                       param->name->kind == NodeKind::Identifier;
    if (!channel) {
        reportOnce(n, diagcode::kWorkerMissingAnnotation,
                   "an inline worker entry must annotate its parameter as "
                   "`thread: Thread<In, Out>`; the annotation is the single "
                   "source of truth for the worker's channel",
                   scan);
    } else if (arrow.isAsync) {
        // The entry runs once at worker start-up: it is not a coroutine.
        reportOnce(n, diagcode::kWorkerEntryShape,
                   "an inline worker entry cannot be async: it runs once at "
                   "worker start-up and its body is the worker thread's setup",
                   scan);
    } else if (!arrow.typeParams.empty()) {
        reportOnce(n, diagcode::kWorkerEntryShape,
                   "an inline worker entry cannot be generic: the worker's "
                   "channel types come from its `Thread<In, Out>` annotation, "
                   "so there is nothing to infer a type parameter from",
                   scan);
    } else if (!named) {
        reportOnce(n, diagcode::kWorkerEntryShape,
                   "an inline worker entry's parameter must be a plain name: it "
                   "is the worker-side handle (`thread: Thread<In, Out>`)",
                   scan);
    } else {
        WorkerSite site;
        site.nodeId = n.nodeId;
        site.in = channel->in;
        site.out = channel->out;
        site.inlineSite = true;
        scan.sites.push_back(InlineWorkerSite{std::move(site), &arrow});
    }
    walkArrow(arrow, scan, true);
}

void walkList(const std::vector<ExprPtr>& list, SiteScan& scan) {
    for (const auto& element : list) {
        if (element) walkExpr(*element, scan);
    }
}

// An identifier inside an inline worker: a module variable of the enclosing
// unit is a capture (CP3909); locals and everything else stay visible.
void walkRef(const Identifier& id, SiteScan& scan) {
    if (!scan.inInline || isLocal(id.name, scan)) return;
    if (scan.moduleVars.count(id.name) == 0) return;
    reportOnce(id, diagcode::kWorkerEnclosingCapture,
               "'" + id.name + "' is module state of the enclosing unit; an "
               "inline worker has its own module state (declare it inside the "
               "worker or send it as a message)",
               scan);
}

// Literal, container and arrow expressions.
void walkContainer(const Expr& e, SiteScan& scan) {
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
        case NodeKind::ArrowFunction:
            walkArrow(static_cast<const ArrowFunction&>(e), scan, false);
            return;
        default:
            return;
    }
}

// Member access, indexing and calls.
void walkAccess(const Expr& e, SiteScan& scan) {
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
        default:
            return;
    }
}

// Operators and control expressions.
void walkOperators(const Expr& e, SiteScan& scan) {
    switch (e.kind) {
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

}  // namespace

void walkExpr(const Expr& e, SiteScan& scan) {
    switch (e.kind) {
        case NodeKind::Identifier:
            walkRef(static_cast<const Identifier&>(e), scan);
            return;
        case NodeKind::NewExpr: {
            const auto& n = static_cast<const NewExpr&>(e);
            if (n.callee && n.callee->kind == NodeKind::Identifier &&
                static_cast<const Identifier&>(*n.callee).name == "Worker" &&
                scan.functions.count("Worker") == 0) {
                walkWorkerNew(n, scan);
                return;
            }
            if (n.callee) walkExpr(*n.callee, scan);
            walkList(n.arguments, scan);
            return;
        }
        default:
            walkContainer(e, scan);
            walkAccess(e, scan);
            walkOperators(e, scan);
            return;
    }
}

namespace {

// The entry unit's module variables: top-level bindings (export-wrapped too).
void collectModuleVars(const Stmt& s, std::unordered_set<std::string>& out) {
    const Stmt* target = &s;
    if (s.kind == NodeKind::ExportDeclaration) {
        const auto& ed = static_cast<const ExportDeclaration&>(s);
        if (!ed.declaration) return;
        target = ed.declaration.get();
    }
    if (target->kind != NodeKind::VariableDeclaration) return;
    const auto& vd = static_cast<const VariableDeclaration&>(*target);
    for (const auto& declPtr : vd.declarations) {
        if (!declPtr || !declPtr->id) continue;
        if (declPtr->id->kind == NodeKind::Identifier) {
            out.insert(static_cast<const Identifier&>(*declPtr->id).name);
        }
    }
}

}  // namespace

void collectInlineWorkerSites(const Program& program, const SemaResult& sema,
                              const std::unordered_set<std::string>& functions,
                              DiagnosticSink& sink,
                              std::vector<InlineWorkerSite>& out) {
    SiteScan scan{sema, sink, functions, {}, {}, {}, {}, false};
    for (const auto& stmtPtr : program.body) {
        if (stmtPtr) collectModuleVars(*stmtPtr, scan.moduleVars);
    }
    for (const auto& stmtPtr : program.body) {
        if (stmtPtr) walkStmt(*stmtPtr, scan);
    }
    out = std::move(scan.sites);
}

}  // namespace cp3::detail
