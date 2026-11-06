// =============================================================================
// src/parser/tests/ast_printer_stmt.cpp - statement node printers. Test-only.
// =============================================================================
#include "ast_printer.h"
#include "ast_printer_internal.h"

#include <cstdio>

namespace cp3test {
using namespace cp3;

namespace {

bool printVarStmt(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::VariableDeclaration: {
            const auto& x = *static_cast<const VariableDeclaration*>(n);
            std::printf("  kind=%s\n", varKindName(x.kind));
            for (const auto& d : x.declarations) printNode(d.get(), ind + 2);
            return true;
        }
        case NodeKind::VariableDeclarator: {
            const auto& x = *static_cast<const VariableDeclarator*>(n);
            std::printf("\n");
            printNode(x.id.get(), ind + 2);
            printNode(x.type.get(), ind + 2);
            printNode(x.init.get(), ind + 2);
            return true;
        }
        default:
            return false;
    }
}

bool printSimpleStmt(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::expressionstatement:
            std::printf("\n");
            printNode(static_cast<const expressionstatement*>(n)->expression.get(), ind + 2);
            return true;
        case NodeKind::BlockStatement:
            std::printf("\n");
            printStmts(static_cast<const BlockStatement*>(n)->body, ind + 2);
            return true;
        case NodeKind::BreakStatement: {
            const auto& x = *static_cast<const BreakStatement*>(n);
            std::printf("  hasLabel=%d label=%s\n", x.hasLabel ? 1 : 0, x.label.c_str());
            return true;
        }
        case NodeKind::ContinueStatement: {
            const auto& x = *static_cast<const ContinueStatement*>(n);
            std::printf("  hasLabel=%d label=%s\n", x.hasLabel ? 1 : 0, x.label.c_str());
            return true;
        }
        case NodeKind::ReturnStatement:
            std::printf("\n");
            printNode(static_cast<const ReturnStatement*>(n)->argument.get(), ind + 2);
            return true;
        case NodeKind::ThrowStatement:
            std::printf("\n");
            printNode(static_cast<const ThrowStatement*>(n)->argument.get(), ind + 2);
            return true;
        case NodeKind::LabeledStatement: {
            const auto& x = *static_cast<const LabeledStatement*>(n);
            std::printf("  label=%s\n", x.label.c_str());
            printNode(x.body.get(), ind + 2);
            return true;
        }
        default:
            return false;
    }
}

bool printBranchStmt(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::IfStatement: {
            const auto& x = *static_cast<const IfStatement*>(n);
            std::printf("\n");
            printNode(x.condition.get(), ind + 2);
            printNode(x.thenBranch.get(), ind + 2);
            printNode(x.elseBranch.get(), ind + 2);
            return true;
        }
        case NodeKind::WhileStatement: {
            const auto& x = *static_cast<const WhileStatement*>(n);
            std::printf("\n");
            printNode(x.condition.get(), ind + 2);
            printNode(x.body.get(), ind + 2);
            return true;
        }
        case NodeKind::DoWhileStatement: {
            const auto& x = *static_cast<const DoWhileStatement*>(n);
            std::printf("\n");
            printNode(x.body.get(), ind + 2);
            printNode(x.condition.get(), ind + 2);
            return true;
        }
        case NodeKind::ForStatement: {
            const auto& x = *static_cast<const ForStatement*>(n);
            std::printf("\n");
            printNode(x.init.get(), ind + 2);
            printNode(x.condition.get(), ind + 2);
            printNode(x.update.get(), ind + 2);
            printNode(x.body.get(), ind + 2);
            return true;
        }
        case NodeKind::ForOfStatement: {
            const auto& x = *static_cast<const ForOfStatement*>(n);
            std::printf("  await=%d\n", x.isAwait ? 1 : 0);
            printNode(x.left.get(), ind + 2);
            printNode(x.right.get(), ind + 2);
            printNode(x.body.get(), ind + 2);
            return true;
        }
        case NodeKind::ForInStatement: {
            const auto& x = *static_cast<const ForInStatement*>(n);
            std::printf("\n");
            printNode(x.left.get(), ind + 2);
            printNode(x.right.get(), ind + 2);
            printNode(x.body.get(), ind + 2);
            return true;
        }
        default:
            return false;
    }
}

bool printSwitchStmt(const Node* n, int ind) {
    switch (n->kind) {
        case NodeKind::SwitchStatement: {
            const auto& x = *static_cast<const SwitchStatement*>(n);
            std::printf("\n");
            printNode(x.discriminant.get(), ind + 2);
            for (const auto& c : x.cases) printNode(c.get(), ind + 2);
            return true;
        }
        case NodeKind::SwitchCase: {
            const auto& x = *static_cast<const SwitchCase*>(n);
            std::printf("  isDefault=%d stmts=%zu\n", x.isDefault ? 1 : 0,
                        x.statements.size());
            printNode(x.test.get(), ind + 2);
            printStmts(x.statements, ind + 2);
            return true;
        }
        case NodeKind::TryStatement: {
            const auto& x = *static_cast<const TryStatement*>(n);
            std::printf("\n");
            printNode(x.tryBlock.get(), ind + 2);
            printNode(x.handler.get(), ind + 2);
            printNode(x.finallyBlock.get(), ind + 2);
            return true;
        }
        case NodeKind::CatchClause: {
            const auto& x = *static_cast<const CatchClause*>(n);
            std::printf("\n");
            printNode(x.param.get(), ind + 2);
            printNode(x.body.get(), ind + 2);
            return true;
        }
        default:
            return false;
    }
}

}  // namespace

bool printStmtNode(const Node* n, int ind) {
    if (printVarStmt(n, ind)) return true;
    if (printSimpleStmt(n, ind)) return true;
    if (printBranchStmt(n, ind)) return true;
    if (printSwitchStmt(n, ind)) return true;
    return false;
}

}  // namespace cp3test
