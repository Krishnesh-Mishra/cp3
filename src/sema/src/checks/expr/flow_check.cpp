// =============================================================================
// src/sema/src/checks/flow_check.cpp - control-flow reachability helpers.
//
// The `allPathsReturn` family used by `analyzeFunctionBody` (stmt_check.cpp) to
// decide whether a body can fall off its end: a terminating statement, a block
// whose last non-empty statement terminates, an if/else with both arms, a
// try/catch whose try or handler terminates, and a switch with a `default`
// where every non-empty clause (JS fall-through allowed) terminates.
// =============================================================================
#include "detail/analyzer.h"

namespace cp3 {

namespace {

// The last statement of the list that is not an `EmptyStatement`, or null.
Stmt* lastNonEmptyStmt(const StmtList& list) {
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        if (*it && (*it)->kind != NodeKind::EmptyStatement) return it->get();
    }
    return nullptr;
}

}  // namespace

bool Analyzer::allPathsReturn(Node* body) {
    if (!body) return false;
    switch (body->kind) {
        case NodeKind::ReturnStatement:
        case NodeKind::ThrowStatement:
            // A `throw` never falls off the end of the function.
            return true;
        case NodeKind::BlockStatement: {
            // The block's outcome is decided by its last non-empty statement.
            auto& bs = static_cast<BlockStatement&>(*body);
            return allPathsReturn(lastNonEmptyStmt(bs.body));
        }
        case NodeKind::IfStatement: {
            // Only an `else` that also terminates closes the if.
            auto& is = static_cast<IfStatement&>(*body);
            return is.elseBranch && allPathsReturn(is.thenBranch.get()) &&
                   allPathsReturn(is.elseBranch.get());
        }
        case NodeKind::TryStatement: {
            auto& ts = static_cast<TryStatement&>(*body);
            // A `return`/`throw` in the try block (or in the catch handler)
            // terminates the function; a `finally` cannot un-terminate it.
            if (allPathsReturn(ts.tryBlock.get())) return true;
            return ts.handler && allPathsReturn(ts.handler->body.get());
        }
        case NodeKind::SwitchStatement:
            return switchCannotFallThrough(static_cast<SwitchStatement&>(*body));
        default:
            return false;
    }
}

bool Analyzer::switchCannotFallThrough(const SwitchStatement& sw) {
    // A `default` plus every non-empty clause terminating means the switch
    // cannot fall through to the end of the function; a clause with no
    // statements falls into the next one (JS fall-through).
    bool hasDefault = false;
    for (const auto& c : sw.cases) {
        if (!c) continue;
        if (c->isDefault) hasDefault = true;
        Stmt* last = lastNonEmptyStmt(c->statements);
        if (last && !allPathsReturn(last)) return false;
    }
    return hasDefault;
}

}  // namespace cp3
