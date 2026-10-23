// =============================================================================
// src/sema/src/detail/parallel_check.h - PRIVATE parallel.* capture-safety model.
//
// Declares the implementation-private scanner used by the parallel.*
// capture-safety check (docs/language-spec §13, "Capture safety, precisely:"):
// it walks one closure body and records writes to captured state (CP3902) and
// unsafe computed-index writes (CP3901). The class is defined across
// parallel_check.cpp (statements + the `Analyzer` hook) and
// parallel_expr_check.cpp (expressions + scope tracking); the small AST
// predicates below are shared by both walks and therefore live here.
//
// Implementation detail: lives under `src/sema/src/detail/` and is never included by
// another module (STYLE.md §3).
// =============================================================================
#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cp3/core/ast.hpp"
#include "cp3/diag/diagnostic.hpp"

namespace cp3 {
namespace parallel_detail {

// One deferred diagnostic produced by the structural walk.
struct Finding {
    const char* code;
    SourceSpan span;
    std::string message;
};

// --- AST predicates shared by the statement and expression walks -------------

// The base identifier a member/index chain is rooted at (`a.b[i]` -> `a`), or
// nullptr when the chain does not start at a simple identifier.
inline Identifier* rootIdentifier(Expr* e) {
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

// `parallel` is a core builtin (D34), used bare as in §13: `parallel.for(...)`.
inline bool isParallelRoot(Expr* e) {
    return e && e->kind == NodeKind::Identifier &&
           static_cast<Identifier&>(*e).name == "parallel";
}

// Array/collection methods that mutate the receiver in place. Calling one on a
// captured object is an unsynchronized write to shared state (CP3902).
inline bool isMutatingMethod(const std::string& name) {
    static const char* const kMutators[] = {
        "push",    "unshift", "pop",        "shift", "splice", "sort",
        "reverse", "fill",    "copyWithin", "add",   "delete", "clear",
        "set",
    };
    for (const char* m : kMutators) {
        if (name == m) return true;
    }
    return false;
}

// The name bound by a (possibly defaulted) binding pattern when it names a
// single identifier; empty otherwise. Used to read parallel.for's induction
// variable off its first parameter.
inline std::string boundName(Node* node) {
    if (!node) return {};
    if (node->kind == NodeKind::Identifier) {
        return static_cast<Identifier&>(*node).name;
    }
    if (node->kind == NodeKind::AssignmentPattern) {
        return boundName(static_cast<AssignmentPattern&>(*node).left.get());
    }
    return {};
}

// Walks one closure body, collecting captured-state writes. `resolves` answers
// whether a name is bound by an enclosing scope (a capture candidate); names
// declared inside the closure are tracked locally so shadowing is exact.
class ClosureScanner {
public:
    using Resolves = std::function<bool(const std::string&)>;

    ClosureScanner(std::string induction, Resolves resolves)
        : induction_(std::move(induction)), resolves_(std::move(resolves)) {}

    // Scans an arrow closure (params + body).
    void scan(ArrowFunction& arrow);
    // Returns and clears the collected findings.
    std::vector<Finding> take() { return std::move(findings_); }

private:
    // --- shared child-walk primitives (parallel_expr_check.cpp) --------------
    void scanMaybeExpr(Expr* e);
    void scanAll(std::initializer_list<Expr*> items);

    // --- closure-local scope tracking (parallel_expr_check.cpp) --------------
    void pushScope();
    void popScope();
    void declareName(const std::string& name);
    void declarePattern(Node* node);
    bool isLocal(const std::string& name) const;
    bool isCaptured(const std::string& name) const;
    bool throughInduction(const IndexExpr& ix) const;
    void addCaptured(const std::string& name, const SourceSpan& span);
    void addIndex(const SourceSpan& span);

    // --- statements (parallel_check.cpp) -------------------------------------
    void scanStmt(Stmt& s);
    void scanControlStmt(Stmt& s);
    void scanBlock(BlockStatement& b);
    void scanIf(IfStatement& is);
    void scanWhile(WhileStatement& w);
    void scanDoWhile(DoWhileStatement& d);
    void scanFor(ForStatement& f);
    void scanVarDecl(VariableDeclaration& vd);
    void scanForInit(Node& init);
    void scanForEach(Node* left, Expr* right, Stmt* body);
    void scanSwitch(SwitchStatement& sw);
    void scanTry(TryStatement& ts);
    void scanFunctionBody(Stmt* body, ParamList& params);
    void scanExport(ExportDeclaration& e);

    // --- expressions (parallel_expr_check.cpp) -------------------------------
    void scanExpr(Expr& e);
    void scanExprChildren(Expr& e);
    void scanOperatorExprs(Expr& e);
    void scanSingleChildExprs(Expr& e);
    void scanContainerExprs(Expr& e);
    void scanCall(CallExpr& c);
    void scanTarget(Expr& target);
    void scanMemberTarget(MemberExpr& m);
    void scanIndexTarget(IndexExpr& ix);

    std::string induction_;
    Resolves resolves_;
    std::vector<std::unordered_set<std::string>> locals_;
    std::vector<Finding> findings_;
};

}  // namespace parallel_detail
}  // namespace cp3
