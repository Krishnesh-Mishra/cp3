// =============================================================================
// src/sema/src/parallel_check.cpp — parallel.* capture-safety diagnostics.
//
// Implements the capture-safety rules of docs/language-spec §13
// ("Capture safety, precisely:" — Rule 2 and the "Reassigning a captured
// let/var inside a parallel.* closure is a compile error too" paragraph):
//
//   * a write (reassignment, compound assignment, ++/--, field write, or an
//     in-place mutating method such as .push) to a variable captured from the
//     enclosing scope is CP3902 (kParallelCaptureReassign);
//   * an indexed write to captured state is CP3901 (kParallelSharedMutation)
//     unless the index is parallel.for's own induction variable, the one
//     provably disjoint write (kParallelSharedMutation);
//   * reading captured state is always allowed.
//
// The check walks the closure body structurally (it does NOT re-run type
// inference), tracking which names are locals of the closure so that a write to
// a local is never mistaken for a captured write. Diagnostics anchor at column
// 1 of the offending node's begin line (D54). The `Analyzer` methods live here;
// the structural walk is a private helper of this translation unit (STYLE.md §8).
// =============================================================================
#include "analyzer.h"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {
namespace {

// One deferred diagnostic produced by the structural walk.
struct Finding {
    const char* code;
    SourceSpan span;
    std::string message;
};

// The base identifier a member/index chain is rooted at (`a.b[i]` -> `a`), or
// nullptr when the chain does not start at a simple identifier.
Identifier* rootIdentifier(Expr* e) {
    if (!e) return nullptr;
    if (e->kind == NodeKind::Identifier) return static_cast<Identifier*>(e);
    if (e->kind == NodeKind::MemberExpr) {
        return rootIdentifier(static_cast<MemberExpr&>(*e).object.get());
    }
    if (e->kind == NodeKind::IndexExpr) {
        return rootIdentifier(static_cast<IndexExpr&>(*e).object.get());
    }
    return nullptr;
}

// `parallel` is a core builtin (D34), used bare as in §13.
bool isParallelObject(Expr* e) {
    return e && e->kind == NodeKind::Identifier &&
           static_cast<Identifier&>(*e).name == "parallel";
}

// Array/collection methods that mutate the receiver in place. Calling one on a
// captured object is an unsynchronized write to shared state (CP3902).
bool isMutatingName(const std::string& name) {
    static const char* const kMutators[] = {
        "push",   "unshift", "pop",    "shift",      "splice", "sort",
        "reverse", "fill",   "copyWithin", "add",    "delete", "clear",
        "set",
    };
    for (const char* m : kMutators) {
        if (name == m) return true;
    }
    return false;
}

// The name bound by a (possibly defaulted) binding pattern, when it names a
// single identifier; empty otherwise. Used to read the induction variable.
std::string patternName(Node* node) {
    if (!node) return {};
    if (node->kind == NodeKind::Identifier) {
        return static_cast<Identifier&>(*node).name;
    }
    if (node->kind == NodeKind::AssignmentPattern) {
        return patternName(static_cast<AssignmentPattern&>(*node).left.get());
    }
    return {};
}

// Walks one closure body, collecting captured-state writes. `resolvesOuter`
// answers whether a name is bound by an enclosing scope (a capture candidate);
// names declared inside the closure are tracked locally so shadowing is exact.
class ClosureScanner {
public:
    using Resolves = std::function<bool(const std::string&)>;

    ClosureScanner(std::string induction, Resolves resolves)
        : induction_(std::move(induction)), resolves_(std::move(resolves)) {}

    void scan(ArrowFunction& arrow) {
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

    std::vector<Finding> take() { return std::move(findings_); }

private:
    void pushScope() { locals_.emplace_back(); }
    void popScope() {
        if (!locals_.empty()) locals_.pop_back();
    }
    void declareName(const std::string& name) {
        if (!name.empty() && !locals_.empty()) locals_.back().insert(name);
    }
    void declarePattern(Node* node) {
        if (!node) return;
        switch (node->kind) {
            case NodeKind::Identifier:
                declareName(static_cast<Identifier&>(*node).name);
                return;
            case NodeKind::AssignmentPattern:
                declarePattern(
                    static_cast<AssignmentPattern&>(*node).left.get());
                return;
            case NodeKind::RestElement:
                declarePattern(
                    static_cast<RestElement&>(*node).argument.get());
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
    bool isLocal(const std::string& name) const {
        for (const auto& scope : locals_) {
            if (scope.count(name) != 0) return true;
        }
        return false;
    }
    bool isCaptured(const std::string& name) const {
        return !isLocal(name) && resolves_(name);
    }
    bool throughInduction(const IndexExpr& ix) const {
        return !induction_.empty() && ix.index &&
               ix.index->kind == NodeKind::Identifier &&
               static_cast<Identifier&>(*ix.index).name == induction_;
    }

    void reportCaptured(const std::string& name, const SourceSpan& span) {
        findings_.push_back(
            Finding{diagcode::kParallelCaptureReassign, span,
                    "cannot reassign or mutate captured variable '" + name +
                        "' inside a parallel.* closure (captures are shared, "
                        "non-owning borrows); return a value and combine it on "
                        "the calling thread instead"});
    }
    void reportIndex(const SourceSpan& span) {
        findings_.push_back(
            Finding{diagcode::kParallelSharedMutation, span,
                    "unsafe indexed write to captured state inside a "
                    "parallel.* closure: only parallel.for's own induction "
                    "variable is allowed as the index (a computed index cannot "
                    "be proven disjoint)"});
    }

    void scanAll(std::initializer_list<Expr*> items) {
        for (Expr* e : items) {
            if (e) scanExpr(*e);
        }
    }

    void scanStmt(Stmt& s);
    void scanBlock(BlockStatement& b) {
        pushScope();
        for (auto& st : b.body) {
            if (st) scanStmt(*st);
        }
        popScope();
    }
    void scanVarDecl(VariableDeclaration& vd) {
        for (auto& d : vd.declarations) {
            if (!d) continue;
            if (d->init) scanExpr(*d->init);
            declarePattern(d->id.get());
        }
    }
    void scanForInit(Node& init);
    void scanForEach(Node* left, Expr* right, Stmt* body) {
        pushScope();
        if (right) scanExpr(*right);
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
    void scanSwitch(SwitchStatement& sw);
    void scanTry(TryStatement& ts);
    void scanFunctionBody(Stmt* body, ParamList& params) {
        pushScope();
        for (auto& p : params) {
            if (p) declarePattern(p->name.get());
        }
        if (body) scanStmt(*body);
        popScope();
    }
    void scanExpr(Expr& e);
    void scanCall(CallExpr& c);
    void scanTarget(Expr& target);
    void scanMemberTarget(MemberExpr& m);
    void scanIndexTarget(IndexExpr& ix);

    std::string induction_;
    Resolves resolves_;
    std::vector<std::unordered_set<std::string>> locals_;
    std::vector<Finding> findings_;
};

void ClosureScanner::scanStmt(Stmt& s) {
    switch (s.kind) {
        case NodeKind::BlockStatement:
            scanBlock(static_cast<BlockStatement&>(s));
            return;
        case NodeKind::ExpressionStatement: {
            auto& es = static_cast<ExpressionStatement&>(s);
            if (es.expression) scanExpr(*es.expression);
            return;
        }
        case NodeKind::VariableDeclaration:
            scanVarDecl(static_cast<VariableDeclaration&>(s));
            return;
        case NodeKind::FunctionDeclaration: {
            auto& fn = static_cast<FunctionDeclaration&>(s);
            scanFunctionBody(fn.body.get(), fn.params);
            return;
        }
        case NodeKind::IfStatement: {
            auto& is = static_cast<IfStatement&>(s);
            if (is.condition) scanExpr(*is.condition);
            if (is.thenBranch) scanStmt(*is.thenBranch);
            if (is.elseBranch) scanStmt(*is.elseBranch);
            return;
        }
        case NodeKind::WhileStatement: {
            auto& w = static_cast<WhileStatement&>(s);
            if (w.condition) scanExpr(*w.condition);
            if (w.body) scanStmt(*w.body);
            return;
        }
        case NodeKind::DoWhileStatement: {
            auto& d = static_cast<DoWhileStatement&>(s);
            if (d.body) scanStmt(*d.body);
            if (d.condition) scanExpr(*d.condition);
            return;
        }
        case NodeKind::ForStatement: {
            auto& f = static_cast<ForStatement&>(s);
            pushScope();
            if (f.init) scanForInit(*f.init);
            if (f.condition) scanExpr(*f.condition);
            if (f.update) scanExpr(*f.update);
            if (f.body) scanStmt(*f.body);
            popScope();
            return;
        }
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
        case NodeKind::ReturnStatement: {
            auto& r = static_cast<ReturnStatement&>(s);
            if (r.argument) scanExpr(*r.argument);
            return;
        }
        case NodeKind::ThrowStatement: {
            auto& t = static_cast<ThrowStatement&>(s);
            if (t.argument) scanExpr(*t.argument);
            return;
        }
        case NodeKind::TryStatement:
            scanTry(static_cast<TryStatement&>(s));
            return;
        case NodeKind::LabeledStatement: {
            auto& l = static_cast<LabeledStatement&>(s);
            if (l.body) scanStmt(*l.body);
            return;
        }
        case NodeKind::ExportDeclaration: {
            auto& e = static_cast<ExportDeclaration&>(s);
            if (e.defaultExpr) scanExpr(*e.defaultExpr);
            if (e.declaration) scanStmt(*e.declaration);
            return;
        }
        default:
            return;
    }
}

void ClosureScanner::scanForInit(Node& init) {
    if (init.kind == NodeKind::VariableDeclaration) {
        scanVarDecl(static_cast<VariableDeclaration&>(init));
    } else if (init.kind == NodeKind::ExpressionStatement) {
        auto& es = static_cast<ExpressionStatement&>(init);
        if (es.expression) scanExpr(*es.expression);
    }
}

void ClosureScanner::scanSwitch(SwitchStatement& sw) {
    if (sw.discriminant) scanExpr(*sw.discriminant);
    for (auto& c : sw.cases) {
        if (!c) continue;
        pushScope();
        if (c->test) scanExpr(*c->test);
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
        if (ts.handler->param) declarePattern(ts.handler->param.get());
        if (ts.handler->body) scanStmt(*ts.handler->body);
        popScope();
    }
    if (ts.finallyBlock) scanStmt(*ts.finallyBlock);
}

void ClosureScanner::scanExpr(Expr& e) {
    switch (e.kind) {
        case NodeKind::AssignmentExpr: {
            auto& a = static_cast<AssignmentExpr&>(e);
            if (a.value) scanExpr(*a.value);
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
        case NodeKind::MemberExpr: {
            auto& m = static_cast<MemberExpr&>(e);
            if (m.object) scanExpr(*m.object);
            return;
        }
        case NodeKind::IndexExpr: {
            auto& ix = static_cast<IndexExpr&>(e);
            scanAll({ix.object.get(), ix.index.get()});
            return;
        }
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
        case NodeKind::UnaryExpr: {
            auto& u = static_cast<UnaryExpr&>(e);
            if (u.operand) scanExpr(*u.operand);
            return;
        }
        case NodeKind::ArrayLiteral: {
            auto& al = static_cast<ArrayLiteral&>(e);
            for (auto& el : al.elements) {
                if (el) scanExpr(*el);
            }
            return;
        }
        case NodeKind::ObjectLiteral: {
            auto& ol = static_cast<ObjectLiteral&>(e);
            for (auto& p : ol.properties) {
                if (!p) continue;
                if (p->computed && p->key) scanExpr(*p->key);
                if (p->value) scanExpr(*p->value);
            }
            return;
        }
        case NodeKind::TemplateLiteral: {
            auto& tl = static_cast<TemplateLiteral&>(e);
            for (auto& ex : tl.expressions) {
                if (ex) scanExpr(*ex);
            }
            return;
        }
        case NodeKind::SpreadElement: {
            auto& sp = static_cast<SpreadElement&>(e);
            if (sp.argument) scanExpr(*sp.argument);
            return;
        }
        case NodeKind::RangeExpr: {
            auto& r = static_cast<RangeExpr&>(e);
            scanAll({r.start.get(), r.end.get()});
            return;
        }
        case NodeKind::AwaitExpr: {
            auto& a = static_cast<AwaitExpr&>(e);
            if (a.argument) scanExpr(*a.argument);
            return;
        }
        case NodeKind::WeakExpr: {
            auto& w = static_cast<WeakExpr&>(e);
            if (w.value) scanExpr(*w.value);
            return;
        }
        case NodeKind::NewExpr: {
            auto& n = static_cast<NewExpr&>(e);
            if (n.callee) scanExpr(*n.callee);
            for (auto& a : n.arguments) {
                if (a) scanExpr(*a);
            }
            return;
        }
        case NodeKind::ArrowFunction:
            scan(static_cast<ArrowFunction&>(e));
            return;
        default:
            return;
    }
}

void ClosureScanner::scanCall(CallExpr& c) {
    bool isParallelCall = false;
    if (c.callee && c.callee->kind == NodeKind::MemberExpr) {
        auto& m = static_cast<MemberExpr&>(*c.callee);
        isParallelCall = isParallelObject(m.object.get());
        if (isMutatingName(m.property)) {
            Identifier* root = rootIdentifier(m.object.get());
            if (root && isCaptured(root->name)) {
                reportCaptured(root->name, c.span);
            }
        }
    }
    if (c.callee) scanExpr(*c.callee);
    for (auto& a : c.arguments) {
        if (!a) continue;
        // A nested parallel.* closure is checked by its own call site with its
        // own induction variable; do not rescane it under this one.
        if (isParallelCall && a->kind == NodeKind::ArrowFunction) continue;
        scanExpr(*a);
    }
}

void ClosureScanner::scanTarget(Expr& target) {
    switch (target.kind) {
        case NodeKind::Identifier: {
            auto& id = static_cast<Identifier&>(target);
            if (isCaptured(id.name)) reportCaptured(id.name, target.span);
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

void ClosureScanner::scanMemberTarget(MemberExpr& m) {
    Identifier* root = rootIdentifier(m.object.get());
    if (root && isCaptured(root->name)) reportCaptured(root->name, m.span);
    if (m.object) scanExpr(*m.object);
}

void ClosureScanner::scanIndexTarget(IndexExpr& ix) {
    Identifier* root = rootIdentifier(ix.object.get());
    if (root && isCaptured(root->name) && !throughInduction(ix)) {
        reportIndex(ix.span);
    }
    scanAll({ix.object.get(), ix.index.get()});
}

}  // namespace

// =============================================================================
// Analyzer entry point
// =============================================================================
void Analyzer::checkParallelCall(CallExpr& call) {
    if (!call.callee || call.callee->kind != NodeKind::MemberExpr) return;
    auto& m = static_cast<MemberExpr&>(*call.callee);
    if (!isParallelObject(m.object.get())) return;
    if (call.nodeId != 0 && !parallelChecked_.insert(call.nodeId).second) return;

    const bool isFor = m.property == "for";
    for (auto& a : call.arguments) {
        if (!a || a->kind != NodeKind::ArrowFunction) continue;
        auto& arrow = static_cast<ArrowFunction&>(*a);
        std::string induction;
        if (isFor && !arrow.params.empty() && arrow.params.front()) {
            induction = patternName(arrow.params.front()->name.get());
        }
        ClosureScanner scanner(induction, [this](const std::string& name) {
            Binding b;
            return lookup(name, b);
        });
        scanner.scan(arrow);
        for (Finding& f : scanner.take()) {
            report(f.code, f.span, std::move(f.message));
        }
    }
}

}  // namespace cp3
