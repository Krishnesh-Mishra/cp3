// =============================================================================
// src/cli/src/commands/lsp_intel_query.cpp - hover, outline and signature help.
//
// OWNERSHIP: cli module (src/cli/**). Implements the matching declarations in
// detail/lsp_intel.hpp.
//
// Every answer is read out of the analysis the checker itself produced: the
// AST for names and positions, `SemaResult::nodeTypes` for inferred types,
// `SemaResult::symbols` for declarations. Types the compiler describes with an
// internal placeholder (the ambient shape behind an unresolved import) are
// withheld instead of leaked, so the editor never shows a name the language
// does not have.
// =============================================================================
#include "detail/lsp/lsp_intel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/lsp.hpp"
#include "cp3/core/source.hpp"
#include "cp3/core/types.hpp"

#include "detail/lsp/lsp_intel_docs.hpp"

namespace cp3::cli {
namespace {

// The name a hover/definition works on. Empty for nodes that carry none.
std::string nameOfNode(const Node& node) {
    switch (node.kind) {
        case NodeKind::Identifier:
            return static_cast<const Identifier&>(node).name;
        case NodeKind::MemberExpr:
            return static_cast<const MemberExpr&>(node).property;
        case NodeKind::FunctionDeclaration:
            return static_cast<const FunctionDeclaration&>(node).name;
        case NodeKind::ClassDeclaration:
            return static_cast<const ClassDeclaration&>(node).name;
        case NodeKind::InterfaceDeclaration:
            return static_cast<const InterfaceDeclaration&>(node).name;
        case NodeKind::EnumDeclaration:
            return static_cast<const EnumDeclaration&>(node).name;
        case NodeKind::TypeAliasDeclaration:
            return static_cast<const TypeAliasDeclaration&>(node).name;
        case NodeKind::VariableDeclarator: {
            const auto& declarator = static_cast<const VariableDeclarator&>(node);
            if (declarator.id != nullptr &&
                declarator.id->kind == NodeKind::Identifier) {
                return static_cast<const Identifier&>(*declarator.id).name;
            }
            return std::string();
        }
        case NodeKind::MethodDeclaration: {
            const auto& method = static_cast<const MethodDeclaration&>(node);
            return keyName(method.key.get());
        }
        case NodeKind::PropertyDeclaration: {
            const auto& property = static_cast<const PropertyDeclaration&>(node);
            return keyName(property.key.get());
        }
        case NodeKind::AccessorDeclaration: {
            const auto& accessor = static_cast<const AccessorDeclaration&>(node);
            return keyName(accessor.key.get());
        }
        default:
            return std::string();
    }
}

// The nearest declaration of `name` at or before `offset` is `nearestSymbolAt`
// and the key name helper is `keyName` (both lsp_intel.cpp, shared with the
// definition and completion units).

void addEntry(std::vector<lsp::SymbolEntry>& out, const std::string& name,
              int kind, const SourceSpan& span) {
    if (name.empty() || !span.valid()) return;
    const SourceSpan selection = nameSpanIn(span, name);
    lsp::SymbolEntry entry;
    entry.name = name;
    entry.kind = kind;
    entry.line = selection.begin.line;
    entry.column = selection.begin.column;
    entry.endLine = span.end.line;
    entry.endColumn = span.end.column;
    out.push_back(std::move(entry));
}

// LSP SymbolKind numbers used here: Class 5, Method 6, Property 7,
// Constructor 9, Enum 10, Interface 11, Function 12, Variable 13,
// Constant 14, EnumMember 22, Struct 23 (type aliases).
constexpr int kKindClass = 5;
constexpr int kKindMethod = 6;
constexpr int kKindProperty = 7;
constexpr int kKindConstructor = 9;
constexpr int kKindEnum = 10;
constexpr int kKindInterface = 11;
constexpr int kKindFunction = 12;
constexpr int kKindVariable = 13;
constexpr int kKindConstant = 14;
constexpr int kKindEnumMember = 22;
constexpr int kKindTypeAlias = 23;

void collectClassMembers(const ClassDeclaration& declaration,
                         std::vector<lsp::SymbolEntry>& out) {
    for (const ClassMemberPtr& member : declaration.members) {
        if (member == nullptr) continue;
        switch (member->kind) {
            case NodeKind::PropertyDeclaration: {
                const auto& property = static_cast<const PropertyDeclaration&>(*member);
                addEntry(out, keyName(property.key.get()), kKindProperty,
                         property.span);
                break;
            }
            case NodeKind::MethodDeclaration: {
                const auto& method = static_cast<const MethodDeclaration&>(*member);
                addEntry(out, keyName(method.key.get()), kKindMethod, method.span);
                break;
            }
            case NodeKind::ConstructorDeclaration:
                addEntry(out, "constructor", kKindConstructor, member->span);
                break;
            case NodeKind::AccessorDeclaration: {
                const auto& accessor = static_cast<const AccessorDeclaration&>(*member);
                addEntry(out, keyName(accessor.key.get()), kKindProperty,
                         accessor.span);
                break;
            }
            default:
                break;
        }
    }
}

void collectSignatureMembers(const std::vector<SignatureMemberPtr>& members,
                             std::vector<lsp::SymbolEntry>& out) {
    for (const SignatureMemberPtr& member : members) {
        if (member == nullptr) continue;
        if (member->kind == NodeKind::PropertySignature) {
            const auto& property = static_cast<const PropertySignature&>(*member);
            addEntry(out, keyName(property.key.get()), kKindProperty, member->span);
        } else if (member->kind == NodeKind::MethodSignature) {
            const auto& method = static_cast<const MethodSignature&>(*member);
            addEntry(out, keyName(method.key.get()), kKindMethod, member->span);
        }
    }
}

void collectSymbols(const Stmt& statement, const SourceFile& file,
                    std::vector<lsp::SymbolEntry>& out) {
    if (!statement.span.valid() || statement.span.file != &file) return;
    switch (statement.kind) {
        case NodeKind::FunctionDeclaration: {
            const auto& function = static_cast<const FunctionDeclaration&>(statement);
            addEntry(out, function.name, kKindFunction, statement.span);
            return;
        }
        case NodeKind::ClassDeclaration: {
            const auto& declaration = static_cast<const ClassDeclaration&>(statement);
            addEntry(out, declaration.name, kKindClass, statement.span);
            collectClassMembers(declaration, out);
            return;
        }
        case NodeKind::InterfaceDeclaration: {
            const auto& declaration = static_cast<const InterfaceDeclaration&>(statement);
            addEntry(out, declaration.name, kKindInterface, statement.span);
            collectSignatureMembers(declaration.members, out);
            return;
        }
        case NodeKind::EnumDeclaration: {
            const auto& declaration = static_cast<const EnumDeclaration&>(statement);
            addEntry(out, declaration.name, kKindEnum, statement.span);
            for (const EnumMemberPtr& member : declaration.members) {
                if (member != nullptr) {
                    addEntry(out, member->name, kKindEnumMember, member->span);
                }
            }
            return;
        }
        case NodeKind::TypeAliasDeclaration: {
            const auto& declaration = static_cast<const TypeAliasDeclaration&>(statement);
            addEntry(out, declaration.name, kKindTypeAlias, statement.span);
            return;
        }
        case NodeKind::VariableDeclaration: {
            const auto& declaration = static_cast<const VariableDeclaration&>(statement);
            const bool isConst = declaration.kind == VarKind::Const;
            for (const VariableDeclaratorPtr& declarator : declaration.declarations) {
                if (declarator == nullptr || declarator->id == nullptr) continue;
                if (declarator->id->kind != NodeKind::Identifier) continue;
                const auto& id = static_cast<const Identifier&>(*declarator->id);
                addEntry(out, id.name, isConst ? kKindConstant : kKindVariable,
                         declarator->span);
            }
            return;
        }
        case NodeKind::ExportDeclaration: {
            const auto& declaration = static_cast<const ExportDeclaration&>(statement);
            if (declaration.declaration != nullptr) {
                collectSymbols(*declaration.declaration, file, out);
            }
            return;
        }
        default:
            return;
    }
}

// The innermost call whose argument region covers the cursor, or nullptr.
const CallExpr* innermostCallAt(const Program& program, const SourceFile& file,
                                std::uint32_t line, std::uint32_t column,
                                std::uint32_t cursor) {
    const CallExpr* best = nullptr;
    forEachNode(program, [&](const Node& node) {
        if (node.kind != NodeKind::CallExpr) return;
        const auto& call = static_cast<const CallExpr&>(node);
        if (!spanContains(call.span, file, line, column)) return;
        if (call.callee == nullptr || !call.callee->span.valid()) return;
        if (cursor < call.callee->span.end.offset) return;  // inside the callee
        if (best == nullptr || call.span.length() < best->span.length()) {
            best = &call;
        }
    });
    return best;
}

constexpr std::uint32_t kNoOffset = 0xFFFFFFFFu;

// The '(' that opens the call's argument list, scanning over an explicit
// generics list (`f<number>(...)`) and whitespace.
std::uint32_t openParenAfter(const std::string& text, std::uint32_t from,
                             std::uint32_t limit) {
    for (std::uint32_t at = from; at < limit && at < text.size(); ++at) {
        const char c = text[at];
        if (c == '(') return at;
        if (c == ' ' || c == '\t') continue;
        if (c == '<') {
            int depth = 1;
            while (++at < limit && at < text.size() && depth > 0) {
                if (text[at] == '<') ++depth;
                else if (text[at] == '>') --depth;
            }
            continue;
        }
        return kNoOffset;
    }
    return kNoOffset;
}

// The index just past a quoted literal starting at `at` (the quote itself).
std::uint32_t skipLiteral(const std::string& text, std::uint32_t at) {
    const char quote = text[at];
    for (std::uint32_t i = at + 1; i < text.size(); ++i) {
        if (text[i] == '\\') {
            ++i;
            continue;
        }
        if (text[i] == quote) return i;
    }
    return kNoOffset;
}

// 0-based index of the argument the cursor sits in: the number of top-level
// commas between the open paren and the cursor. Best effort on templates.
int countArguments(const std::string& text, std::uint32_t open,
                   std::uint32_t cursor) {
    int depth = 0;
    int commas = 0;
    for (std::uint32_t at = open + 1; at < cursor && at < text.size(); ++at) {
        const char c = text[at];
        if (c == '"' || c == '\'' || c == '`') {
            const std::uint32_t end = skipLiteral(text, at);
            if (end == kNoOffset) break;
            at = end;
            continue;
        }
        if (c == '/' && at + 1 < text.size() && text[at + 1] == '/') break;
        if (c == '/' && at + 1 < text.size() && text[at + 1] == '*') {
            const std::size_t end = text.find("*/", at + 2);
            if (end == std::string::npos) break;
            at = static_cast<std::uint32_t>(end + 1);
            continue;
        }
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            if (depth > 0) --depth;
        } else if (c == ',' && depth == 0) {
            ++commas;
        }
    }
    return commas;
}

}  // namespace

const Type* expectedArgumentTypeAt(const AnalyzedDocument& document,
                                   std::uint32_t cursor) {
    if (!analysisReady(document)) return nullptr;
    const CallExpr* best = nullptr;
    forEachNode(*document.entry->ast, [&](const Node& node) {
        if (node.kind != NodeKind::CallExpr || !node.span.valid() ||
            node.span.file != document.file || cursor < node.span.begin.offset ||
            cursor > node.span.end.offset) {
            return;
        }
        const auto& call = static_cast<const CallExpr&>(node);
        if (call.callee == nullptr || !call.callee->span.valid() ||
            cursor < call.callee->span.end.offset) {
            return;
        }
        if (best == nullptr || call.span.length() < best->span.length()) {
            best = &call;
        }
    });
    if (best == nullptr || best->callee == nullptr) return nullptr;
    const std::uint32_t open = openParenAfter(
        document.file->text, best->callee->span.end.offset, cursor);
    if (open == kNoOffset) return nullptr;
    const int argument = countArguments(document.file->text, open, cursor);
    if (argument < 0) return nullptr;
    const Type* callee = document.sema.typeOf(best->callee->nodeId);
    if (callee == nullptr || callee->kind != TypeKind::Function ||
        static_cast<std::size_t>(argument) + 1 >= callee->parts.size()) {
        return nullptr;
    }
    const Type* expected = &callee->parts[static_cast<std::size_t>(argument)];
    if (expected->isUnknown() || expected->isError()) return nullptr;
    return expected;
}

std::optional<lsp::HoverAnswer> hoverAt(const std::string& path,
                                        const std::string& text,
                                        std::uint32_t line,
                                        std::uint32_t column) {
    auto document = analyzeDocument(path, text);
    if (!analysisReady(*document)) return std::nullopt;
    const Node* node =
        innermostNodeAt(*document->entry->ast, *document->file, line, column);
    if (node == nullptr) return std::nullopt;
    const std::string name = nameOfNode(*node);
    const Type* type = document->sema.typeOf(node->nodeId);
    const SymbolInfo* symbol = nullptr;
    if (type == nullptr && !name.empty()) {
        const std::uint32_t offset =
            offsetOf(*document->file, line, column);
        symbol = nearestSymbolAt(document->sema, name, *document->file, offset);
        if (symbol != nullptr) type = &symbol->type;
    }
    // The documentation: the std surface's own description, or the doc
    // comment of the declaration the name refers to.
    const DocAnswer doc = documentationAt(*document, *node, name, symbol);
    lsp::HoverAnswer answer;
    if (type == nullptr || isPlaceholderType(*type)) {
        // An ambient std surface: the compiler has no precise type for it, so
        // the documented signature is what the editor shows.
        if (doc.empty()) return std::nullopt;
        answer.contents = doc.signature;
        answer.documentation = doc.body;
        return answer;
    }
    answer.contents = name.empty() ? describe(*type) : name + ": " + describe(*type);
    answer.documentation = doc.signature.empty()
                               ? doc.body
                               : renderStdDoc(doc.signature, doc.body);
    return answer;
}

std::vector<lsp::SymbolEntry> symbolsAt(const std::string& path,
                                        const std::string& text) {
    std::vector<lsp::SymbolEntry> entries;
    auto document = analyzeDocument(path, text);
    if (!analysisReady(*document)) return entries;
    for (const StmtPtr& statement : document->entry->ast->body) {
        if (statement != nullptr) {
            collectSymbols(*statement, *document->file, entries);
        }
    }
    return entries;
}

std::optional<lsp::SignatureAnswer> signatureAt(const std::string& path,
                                                const std::string& text,
                                                std::uint32_t line,
                                                std::uint32_t column) {
    auto document = analyzeDocument(path, text);
    if (!analysisReady(*document)) return std::nullopt;
    const std::uint32_t cursor = offsetOf(*document->file, line, column);
    const CallExpr* call = innermostCallAt(*document->entry->ast,
                                           *document->file, line, column, cursor);
    if (call == nullptr || call->callee == nullptr) return std::nullopt;
    const Type* calleeType = document->sema.typeOf(call->callee->nodeId);
    const SymbolInfo* symbol = nullptr;
    if (call->callee->kind == NodeKind::Identifier) {
        const auto& id = static_cast<const Identifier&>(*call->callee);
        symbol = nearestSymbolAt(document->sema, id.name, *document->file, cursor);
        if (calleeType == nullptr && symbol != nullptr) {
            calleeType = &symbol->type;
        }
    }
    if (calleeType == nullptr || calleeType->kind != TypeKind::Function ||
        calleeType->parts.size() < 2) {
        return std::nullopt;
    }
    const std::uint32_t open =
        openParenAfter(document->file->text, call->callee->span.end.offset, cursor);
    if (open == kNoOffset) return std::nullopt;
    lsp::SignatureAnswer answer;
    answer.label = describe(*calleeType);
    for (std::size_t i = 0; i + 1 < calleeType->parts.size(); ++i) {
        answer.parameters.push_back(describe(calleeType->parts[i]));
    }
    answer.activeParameter =
        countArguments(document->file->text, open, cursor);
    // The documentation of the callee: the std surface's own description, or
    // the doc comment of the declaration the name refers to.
    const DocAnswer doc =
        documentationAt(*document, *call->callee,
                        call->callee->kind == NodeKind::Identifier
                            ? static_cast<const Identifier&>(*call->callee).name
                            : std::string(),
                        symbol);
    answer.documentation = doc.signature.empty()
                               ? doc.body
                               : renderStdDoc(doc.signature, doc.body);
    answer.parameterDocs = parameterDocs(*document, symbol, answer.parameters.size());
    return answer;
}

}  // namespace cp3::cli
