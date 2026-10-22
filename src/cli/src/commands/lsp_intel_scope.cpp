// =============================================================================
// src/cli/src/commands/lsp_intel_scope.cpp - what is in scope at the cursor.
//
// OWNERSHIP: cli module (src/cli/**). Implements scopeCandidates
// (detail/lsp_intel_complete.hpp).
//
// The completion popup of a JavaScript developer leads with the names the
// file itself brings into scope: the parameters of the enclosing function,
// the locals of the enclosing blocks, the binding of the enclosing loop or
// catch clause. This unit collects them, and attaches the type the compiler
// inferred to every one of them.
//
// The names come from the AST chain around the cursor (which blocks, functions
// and loops enclose it) and from the compiler's own type table, so nothing
// here guesses: a name is offered because the compiler bound it there.
// =============================================================================
#include "detail/lsp_intel_complete.hpp"
#include "detail/lsp_intel_docs.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"
#include "cp3/source.hpp"
#include "cp3/types.hpp"

namespace cp3::cli {
namespace {

// The identifiers a binding pattern binds.
void appendPatternNames(const Node* pattern, std::vector<const Node*>& names) {
    if (pattern == nullptr) return;
    switch (pattern->kind) {
        case NodeKind::Identifier:
            names.push_back(pattern);
            return;
        case NodeKind::AssignmentPattern:
            appendPatternNames(
                static_cast<const AssignmentPattern&>(*pattern).left.get(), names);
            return;
        case NodeKind::RestElement:
            appendPatternNames(
                static_cast<const RestElement&>(*pattern).argument.get(), names);
            return;
        case NodeKind::ArrayPattern: {
            const auto& array = static_cast<const ArrayPattern&>(*pattern);
            for (const NodePtr& element : array.elements) {
                appendPatternNames(element.get(), names);
            }
            appendPatternNames(array.rest.get(), names);
            return;
        }
        case NodeKind::ObjectPattern: {
            const auto& object = static_cast<const ObjectPattern&>(*pattern);
            for (const ObjectPatternPropertyPtr& property : object.properties) {
                if (property == nullptr) continue;
                appendPatternNames(property->value.get(), names);
            }
            appendPatternNames(object.rest.get(), names);
            return;
        }
        default:
            return;
    }
}

// The name an identifier node binds.
std::string identifierName(const Node& node) {
    if (node.kind != NodeKind::Identifier) return std::string();
    return static_cast<const Identifier&>(node).name;
}

// The type the compiler inferred for a binding, when it knows one.
std::string typeDetail(const SemaResult& sema, std::uint32_t nodeId,
                       const std::string& fallback) {
    const Type* type = sema.typeOf(nodeId);
    if (type == nullptr || type->isUnknown() || type->isError()) return fallback;
    if (type->kind == TypeKind::Object && !type->name.empty() &&
        type->name.front() == '$') {
        return fallback;  // the compiler's placeholder for an unresolved import
    }
    return describe(*type);
}

// How a binding is presented in the popup.
struct BindingStyle {
    int kind = 6;          // the LSP CompletionItemKind
    const char* fallback = "let";  // the detail when the type is unknown
};

// The rendered doc comment of a declaration, or an empty string. The doc
// table lives in the sema result, so the scope builder needs no document.
std::string docFor(const SemaResult& sema, std::uint32_t nodeId) {
    const auto it = sema.docs.find(nodeId);
    if (it == sema.docs.end() || it->second.empty()) return {};
    return renderDocComment(it->second);
}

// Appends the names a pattern binds, each with the type the compiler gave it.
void appendBoundNames(const Node* pattern, const SemaResult& sema,
                      const BindingStyle& style,
                      std::vector<lsp::CompletionCandidate>& out) {
    std::vector<const Node*> names;
    appendPatternNames(pattern, names);
    for (const Node* node : names) {
        const std::string name = identifierName(*node);
        if (name.empty()) continue;
        lsp::CompletionCandidate candidate;
        candidate.label = name;
        candidate.kind = style.kind;
        candidate.detail = typeDetail(sema, node->nodeId, style.fallback);
        candidate.documentation = docFor(sema, node->nodeId);
        out.push_back(std::move(candidate));
    }
}

// The names a variable declaration binds.
void appendVariableNames(const VariableDeclaration& declaration,
                         const SemaResult& sema,
                         std::vector<lsp::CompletionCandidate>& out) {
    const bool isConst = declaration.kind == VarKind::Const;
    const BindingStyle style{isConst ? 21 : 6, isConst ? "const" : "let"};
    for (const VariableDeclaratorPtr& declarator : declaration.declarations) {
        if (declarator == nullptr || declarator->id == nullptr) continue;
        appendBoundNames(declarator->id.get(), sema, style, out);
    }
}

// The names a declaration's parameters bind.
void appendParameterNames(const Node& node, const SemaResult& sema,
                           std::vector<lsp::CompletionCandidate>& out) {
    const ParamList* params = nullptr;
    switch (node.kind) {
        case NodeKind::FunctionDeclaration:
            params = &static_cast<const FunctionDeclaration&>(node).params;
            break;
        case NodeKind::MethodDeclaration:
            params = &static_cast<const MethodDeclaration&>(node).params;
            break;
        case NodeKind::ConstructorDeclaration:
            params = &static_cast<const ConstructorDeclaration&>(node).params;
            break;
        case NodeKind::AccessorDeclaration:
            params = &static_cast<const AccessorDeclaration&>(node).params;
            break;
        case NodeKind::ArrowFunction:
            params = &static_cast<const ArrowFunction&>(node).params;
            break;
        default:
            return;
    }
    const BindingStyle style{6, "parameter"};
    for (const ParameterPtr& parameter : *params) {
        if (parameter == nullptr) continue;
        appendBoundNames(parameter->name.get(), sema, style, out);
    }
}

// The locals a block declares above the cursor.
void appendBlockNames(const BlockStatement& block, const SemaResult& sema,
                       std::uint32_t offset,
                       std::vector<lsp::CompletionCandidate>& out) {
    for (const StmtPtr& statement : block.body) {
        if (statement == nullptr || !statement->span.valid()) continue;
        if (statement->kind != NodeKind::VariableDeclaration) continue;
        const auto& declaration = static_cast<const VariableDeclaration&>(*statement);
        for (const VariableDeclaratorPtr& declarator : declaration.declarations) {
            if (declarator == nullptr || !declarator->span.valid()) continue;
            if (declarator->span.end.offset > offset) continue;  // not bound yet
            appendVariableNames(declaration, sema, out);
            break;
        }
    }
}

// The binding a `for ... of` / `for ... in` loop introduces.
void appendLoopBinding(const Node& node, const SemaResult& sema,
                       std::vector<lsp::CompletionCandidate>& out) {
    const Node* left = nullptr;
    if (node.kind == NodeKind::ForOfStatement) {
        left = static_cast<const ForOfStatement&>(node).left.get();
    } else if (node.kind == NodeKind::ForInStatement) {
        left = static_cast<const ForInStatement&>(node).left.get();
    }
    if (left == nullptr) return;
    if (left->kind == NodeKind::VariableDeclaration) {
        appendVariableNames(static_cast<const VariableDeclaration&>(*left), sema,
                            out);
        return;
    }
    appendBoundNames(left, sema, BindingStyle{6, "let"}, out);
}

// The AST nodes that enclose `offset`, outermost first.
std::vector<const Node*> enclosingChain(const AnalyzedDocument& document,
                                        std::uint32_t offset) {
    std::vector<const Node*> chain;
    forEachNode(*document.entry->ast, [&](const Node& node) {
        if (!node.span.valid() || node.span.file != document.file) return;
        if (node.span.begin.offset > offset) return;
        if (offset > node.span.end.offset) return;
        chain.push_back(&node);
    });
    std::stable_sort(chain.begin(), chain.end(),
                     [](const Node* a, const Node* b) {
                         return a->span.length() > b->span.length();
                     });
    return chain;
}

// The names one enclosing node brings into scope.
void appendLocalNames(const Node& node, const SemaResult& sema,
                       std::uint32_t offset,
                       std::vector<lsp::CompletionCandidate>& out) {
    switch (node.kind) {
        case NodeKind::BlockStatement:
            appendBlockNames(static_cast<const BlockStatement&>(node), sema,
                             offset, out);
            return;
        case NodeKind::CatchClause: {
            const auto& clause = static_cast<const CatchClause&>(node);
            appendBoundNames(clause.param.get(), sema, BindingStyle{6, "catch"},
                             out);
            return;
        }
        case NodeKind::ForOfStatement:
        case NodeKind::ForInStatement:
            appendLoopBinding(node, sema, out);
            return;
        case NodeKind::ClassDeclaration: {
            const auto& declaration = static_cast<const ClassDeclaration&>(node);
            lsp::CompletionCandidate candidate;
            candidate.label = declaration.name;
            candidate.kind = 7;
            candidate.detail = "class";
            out.push_back(std::move(candidate));
            return;
        }
        default:
            return;
    }
}

// True for the nodes whose parameters are in scope where they appear.
bool bindsParameters(NodeKind kind) {
    return kind == NodeKind::FunctionDeclaration ||
           kind == NodeKind::MethodDeclaration ||
           kind == NodeKind::ConstructorDeclaration ||
           kind == NodeKind::AccessorDeclaration ||
           kind == NodeKind::ArrowFunction;
}

}  // namespace

std::vector<lsp::CompletionCandidate> scopeCandidates(
    const AnalyzedDocument& document, CompletionPosition position,
    std::uint32_t offset) {
    std::vector<lsp::CompletionCandidate> out;
    const std::vector<const Node*> chain = enclosingChain(document, offset);
    const bool typesOnly = position == CompletionPosition::TypePosition;
    // The parameters of the enclosing functions, innermost first.
    for (std::size_t i = chain.size(); i-- > 0;) {
        const Node* node = chain[i];
        if (!bindsParameters(node->kind) || typesOnly) continue;
        appendParameterNames(*node, document.sema, out);
    }
    // The locals of the enclosing blocks, innermost block first. In a type
    // position only the names that name a type belong there.
    for (std::size_t i = chain.size(); i-- > 0;) {
        const Node* node = chain[i];
        if (typesOnly && node->kind != NodeKind::ClassDeclaration) continue;
        appendLocalNames(*node, document.sema, offset, out);
    }
    return out;
}

}  // namespace cp3::cli
