// =============================================================================
// src/codegen/src/detail/worker_scan_internal.h - inline worker pre-scan state.
//
// OWNERSHIP: codegen module. PRIVATE header shared by emit/worker_scan.cpp
// (state, binding patterns, expression captures) and emit/worker_scan_stmt.cpp
// (the statement walk), split for the file budget (STYLE.md sec.2).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"
#include "cp3/diagnostic.hpp"
#include "detail/worker.h"

namespace cp3::detail {

// The inline-scan state: the sema side table (annotations), the diagnostic
// sink, the entry unit's top-level functions, the module variables an inline
// worker may not capture, and the scope stack of names declared inside the
// worker.
struct SiteScan {
    const SemaResult& sema;
    DiagnosticSink& sink;
    const std::unordered_set<std::string>& functions;
    std::unordered_set<std::string> moduleVars;
    std::unordered_set<std::uint32_t> reported;
    std::vector<std::unordered_set<std::string>> scopes;
    std::vector<InlineWorkerSite> sites;
    bool inInline = false;  // walking inside an inline worker arrow
};

bool isLocal(const std::string& name, const SiteScan& scan);
void declare(const std::string& name, SiteScan& scan);
void reportOnce(const Node& node, const std::string& code,
                const std::string& message, SiteScan& scan);

// One binding pattern's declared names, and one arrow body's expressions.
void walkPattern(const NodePtr& node, SiteScan& scan);
void walkExpr(const Expr& e, SiteScan& scan);
void walkStmt(const Stmt& s, SiteScan& scan);

struct ScopeGuard {
    SiteScan& scan;
    explicit ScopeGuard(SiteScan& s) : scan(s) { scan.scopes.emplace_back(); }
    ~ScopeGuard() { scan.scopes.pop_back(); }
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
};

}  // namespace cp3::detail
