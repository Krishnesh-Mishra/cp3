// =============================================================================
// src/cli/src/commands/lsp_intel_position.cpp - where the cursor is.
//
// OWNERSHIP: cli module (src/cli/**). Implements classifyPosition
// (detail/lsp_intel_complete.hpp).
//
// The completion popup has to answer the question a JavaScript developer
// expects: "what can I write here?". That needs the syntactic POSITION of the
// cursor, so that only the keywords that can legally appear there are
// offered.
//
// The position comes from two sources the compiler already has: the token
// stream (what may follow the cursor) and the AST (what encloses it - a
// class body, a try block, a switch body, a type annotation). A keyword that
// cannot appear at the cursor is never offered.
// =============================================================================
#include "detail/lsp/lsp_intel_complete.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/core/source.hpp"
#include "cp3/core/token.hpp"
#include "cp3/core/types.hpp"

namespace cp3::cli {
namespace {

// -----------------------------------------------------------------------------
// Token helpers
// -----------------------------------------------------------------------------

bool isTrivia(TokenKind kind) {
    return kind == TokenKind::LineComment || kind == TokenKind::BlockComment;
}

// The index of the last significant token before `index` (tokens.size() when
// there is none).
std::size_t significantBefore(const std::vector<Token>& tokens,
                               std::size_t index) {
    for (std::size_t i = index; i-- > 0;) {
        if (isTrivia(tokens[i].kind)) continue;
        return i;
    }
    return tokens.size();
}

// The index of the first significant token at or after `index`.
std::size_t significantAfter(const std::vector<Token>& tokens,
                              std::size_t index) {
    for (std::size_t i = index; i < tokens.size(); ++i) {
        if (tokens[i].kind == TokenKind::EndOfFile) break;
        if (isTrivia(tokens[i].kind)) continue;
        return i;
    }
    return tokens.size();
}

// The tokens that must be followed by an expression.
const TokenKind kExpressionOperators[] = {
    TokenKind::Assign, TokenKind::PlusAssign, TokenKind::MinusAssign,
    TokenKind::StarAssign, TokenKind::SlashAssign, TokenKind::PercentAssign,
    TokenKind::StarStarAssign, TokenKind::AmpersandAssign,
    TokenKind::PipeAssign, TokenKind::CaretAssign, TokenKind::ShiftLeftAssign,
    TokenKind::ShiftRightAssign, TokenKind::UnsignedShiftRightAssign,
    TokenKind::AndAndAssign, TokenKind::OrOrAssign,
    TokenKind::QuestionQuestionAssign, TokenKind::Plus, TokenKind::Minus,
    TokenKind::Star, TokenKind::Slash, TokenKind::Percent, TokenKind::StarStar,
    TokenKind::Ampersand, TokenKind::Pipe, TokenKind::Caret, TokenKind::Tilde,
    TokenKind::ShiftLeft, TokenKind::ShiftRight, TokenKind::UnsignedShiftRight,
    TokenKind::Bang, TokenKind::AndAnd, TokenKind::OrOr,
    TokenKind::QuestionQuestion, TokenKind::Less, TokenKind::Greater,
    TokenKind::LessEqual, TokenKind::GreaterEqual, TokenKind::Equal,
    TokenKind::NotEqual, TokenKind::StrictEqual, TokenKind::StrictNotEqual,
    TokenKind::Arrow, TokenKind::Question,
};

// True when the token must be followed by an expression.
bool expectsExpression(TokenKind kind) {
    for (const TokenKind candidate : kExpressionOperators) {
        if (candidate == kind) return true;
    }
    return false;
}

// True when a line break separates `from` and `to`.
bool lineBreakBetween(const std::string& text, std::uint32_t from,
                       std::uint32_t to) {
    const std::uint32_t limit = to < text.size()
                                     ? to
                                     : static_cast<std::uint32_t>(text.size());
    for (std::uint32_t at = from; at < limit; ++at) {
        if (text[at] == '\n') return true;
    }
    return false;
}

// The token that ends right before the cursor (comments skipped), or nullptr
// at the start of the file.
const Token* previousToken(const std::vector<Token>& tokens,
                           std::uint32_t offset) {
    const Token* found = nullptr;
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::EndOfFile) break;
        if (token.span.end.offset > offset) break;
        if (isTrivia(token.kind)) continue;
        found = &token;
    }
    return found;
}

// True when the `(` at `index` opens an arrow function's parameter list: the
// matching `)` is followed by `=>`.
bool opensArrowParameters(const std::vector<Token>& tokens, std::size_t index) {
    int depth = 0;
    const std::size_t limit = tokens.size();
    for (std::size_t i = index; i < limit && i < index + 512; ++i) {
        const TokenKind kind = tokens[i].kind;
        if (kind == TokenKind::LParen) {
            ++depth;
            continue;
        }
        if (kind != TokenKind::RParen) continue;
        --depth;
        if (depth > 0) continue;
        const std::size_t next = significantAfter(tokens, i + 1);
        return next != limit && tokens[next].kind == TokenKind::Arrow;
    }
    return false;
}

// True when the `(` at `index` opens a declaration's parameter list:
// `function f(`, `get x(`, `set x(`, `constructor(` or an arrow's `(a, b) =>`.
bool opensParameterList(const std::vector<Token>& tokens, std::size_t index) {
    const std::size_t prevIndex = significantBefore(tokens, index);
    if (prevIndex == tokens.size()) return false;
    const Token& prev = tokens[prevIndex];
    if (prev.kind == TokenKind::Identifier) {
        const std::size_t beforeIndex = significantBefore(tokens, prevIndex);
        if (beforeIndex != tokens.size()) {
            const TokenKind before = tokens[beforeIndex].kind;
            if (before == TokenKind::KwFunction || before == TokenKind::KwGet ||
                before == TokenKind::KwSet) {
                return true;
            }
        }
        if (prev.lexeme == "constructor") return true;
    }
    return opensArrowParameters(tokens, index);
}

// An open bracket at the cursor.
struct OpenBracket {
    TokenKind kind = TokenKind::LParen;
    bool parameterList = false;
    std::size_t tokenIndex = 0;
};

// The brackets that are open at the cursor, outermost first.
std::vector<OpenBracket> openBrackets(const std::vector<Token>& tokens,
                                       std::uint32_t offset) {
    std::vector<OpenBracket> stack;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& token = tokens[i];
        if (token.kind == TokenKind::EndOfFile) break;
        if (token.span.end.offset > offset) break;
        switch (token.kind) {
            case TokenKind::LParen:
                stack.push_back({TokenKind::LParen, opensParameterList(tokens, i), i});
                break;
            case TokenKind::LBracket:
                stack.push_back({TokenKind::LBracket, false, i});
                break;
            case TokenKind::LBrace:
                stack.push_back({TokenKind::LBrace, false, i});
                break;
            case TokenKind::RParen:
            case TokenKind::RBracket:
            case TokenKind::RBrace:
                if (!stack.empty()) stack.pop_back();
                break;
            default:
                break;
        }
    }
    return stack;
}

// The position a keyword implies: the keyword opens a statement, an
// expression, a declaration's name, a type or an import clause.
CompletionPosition keywordPosition(TokenKind kind) {
    switch (kind) {
        case TokenKind::KwReturn:
        case TokenKind::KwThrow:
        case TokenKind::KwCase:
        case TokenKind::KwNew:
        case TokenKind::KwTypeof:
        case TokenKind::KwAwait:
        case TokenKind::KwIn:
        case TokenKind::KwOf:
        case TokenKind::KwElse:
        case TokenKind::KwDo:
        case TokenKind::KwVoid:
        case TokenKind::KwDelete:
        case TokenKind::KwIf:
        case TokenKind::KwWhile:
        case TokenKind::KwFor:
        case TokenKind::KwSwitch:
        case TokenKind::KwCatch:
            return CompletionPosition::ExpressionStart;
        case TokenKind::KwLet:
        case TokenKind::KwConst:
        case TokenKind::KwVar:
        case TokenKind::KwFunction:
        case TokenKind::KwClass:
        case TokenKind::KwInterface:
        case TokenKind::KwEnum:
        case TokenKind::KwType:
            return CompletionPosition::DeclarationName;
        case TokenKind::KwImport:
        case TokenKind::KwExport:
        case TokenKind::KwFrom:
        case TokenKind::KwAs:
            return CompletionPosition::ImportClause;
        case TokenKind::KwExtends:
        case TokenKind::KwImplements:
            return CompletionPosition::TypePosition;
        default:
            return CompletionPosition::Unknown;
    }
}

// The syntactic position, from the token stream alone.
CompletionPosition tokenPosition(const std::vector<Token>& tokens,
                                  const std::string& text,
                                  std::uint32_t offset,
                                  const std::vector<OpenBracket>& brackets) {
    if (!brackets.empty() && brackets.back().parameterList) {
        // While the AST is unavailable (a common state as a declaration is
        // being typed), distinguish `name: Type` from the parameter name slot
        // using the current comma-delimited parameter tokens.
        const std::size_t open = brackets.back().tokenIndex;
        int nested = 0;
        bool sawColon = false;
        for (std::size_t i = open + 1; i < tokens.size(); ++i) {
            const Token& token = tokens[i];
            if (token.kind == TokenKind::EndOfFile ||
                token.span.begin.offset >= offset) break;
            switch (token.kind) {
                case TokenKind::LParen:
                case TokenKind::LBracket:
                case TokenKind::LBrace:
                    ++nested;
                    break;
                case TokenKind::RParen:
                case TokenKind::RBracket:
                case TokenKind::RBrace:
                    if (nested > 0) --nested;
                    break;
                case TokenKind::Comma:
                    if (nested == 0) sawColon = false;
                    break;
                case TokenKind::Colon:
                    if (nested == 0) sawColon = true;
                    break;
                case TokenKind::Assign:
                    if (nested == 0 && sawColon) sawColon = false;
                    break;
                default:
                    break;
            }
        }
        if (sawColon) return CompletionPosition::TypePosition;
        return CompletionPosition::ParameterList;
    }
    const Token* prev = previousToken(tokens, offset);
    if (prev == nullptr) return CompletionPosition::StatementStart;
    // A line break ends the previous statement (cp3 has ASI).
    if (lineBreakBetween(text, prev->span.end.offset, offset) &&
        !expectsExpression(prev->kind)) {
        return CompletionPosition::StatementStart;
    }
    switch (prev->kind) {
        case TokenKind::Semicolon:
        case TokenKind::LBrace:
        case TokenKind::RBrace:
            return CompletionPosition::StatementStart;
        case TokenKind::LParen:
        case TokenKind::LBracket:
        case TokenKind::Comma:
        case TokenKind::Arrow:
        case TokenKind::Question:
        case TokenKind::Colon:
            return CompletionPosition::ExpressionStart;
        default:
            break;
    }
    const CompletionPosition keyword = keywordPosition(prev->kind);
    if (keyword != CompletionPosition::Unknown) return keyword;
    // An identifier, a literal or a closing bracket: the expression goes on.
    return CompletionPosition::ExpressionStart;
}

// -----------------------------------------------------------------------------
// The AST around the cursor
// -----------------------------------------------------------------------------

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

// The innermost node of `kind` in the chain, or nullptr.
const Node* innermostOf(const std::vector<const Node*>& chain, NodeKind kind) {
    for (std::size_t i = chain.size(); i-- > 0;) {
        if (chain[i]->kind == kind) return chain[i];
    }
    return nullptr;
}

// True when the cursor is inside a type node (a type is expected there).
bool insideTypeNode(const std::vector<const Node*>& chain) {
    for (std::size_t i = chain.size(); i-- > 0;) {
        switch (chain[i]->kind) {
            case NodeKind::NamedType:
            case NodeKind::UnionType:
            case NodeKind::ArrayType:
            case NodeKind::TupleType:
            case NodeKind::FunctionType:
            case NodeKind::ObjectType:
            case NodeKind::PrefixedType:
            case NodeKind::LiteralType:
            case NodeKind::ParenthesizedType:
                return true;
            default:
                break;
        }
    }
    return false;
}

// True when the cursor sits directly in a class body (not inside a member).
bool insideClassBody(const std::vector<const Node*>& chain,
                      std::uint32_t offset) {
    const Node* node = innermostOf(chain, NodeKind::ClassDeclaration);
    if (node == nullptr) return false;
    const auto& declaration = static_cast<const ClassDeclaration&>(*node);
    for (const ClassMemberPtr& member : declaration.members) {
        if (member == nullptr || !member->span.valid()) continue;
        if (member->span.begin.offset > offset) continue;
        if (offset > member->span.end.offset) continue;
        return false;  // inside a member: a method body, not the class body
    }
    return true;
}

// True when the cursor sits in the type-annotation slot of a parameter or a
// variable declarator: after the declared name, before the default value.
bool inAnnotationSlot(const Node& node, std::uint32_t offset) {
    const Node* name = nullptr;
    const Node* type = nullptr;
    const Node* after = nullptr;
    if (node.kind == NodeKind::Parameter) {
        const auto& parameter = static_cast<const Parameter&>(node);
        name = parameter.name.get();
        type = parameter.type.get();
        after = parameter.defaultValue.get();
    } else if (node.kind == NodeKind::VariableDeclarator) {
        const auto& declarator = static_cast<const VariableDeclarator&>(node);
        name = declarator.id.get();
        type = declarator.type.get();
        after = declarator.init.get();
    }
    if (name == nullptr || type != nullptr || !name->span.valid()) return false;
    if (offset <= name->span.end.offset) return false;  // still at the name
    if (after != nullptr && after->span.valid() &&
        offset >= after->span.begin.offset) {
        return false;  // already at the default value
    }
    return true;
}

// Between a closing statement and its optional attachment keyword, allow
// whitespace and a partially typed prefix only. This works during editing,
// before the parser can build a catch/else node.
bool attachmentPrefixAt(const std::string& text, std::uint32_t from,
                        std::uint32_t to, const char* first,
                        const char* second = nullptr) {
    if (from > to || to > text.size()) return false;
    std::string typed;
    for (std::uint32_t at = from; at < to; ++at) {
        const unsigned char ch = static_cast<unsigned char>(text[at]);
        if (std::isspace(ch)) continue;
        if (!std::isalpha(ch)) return false;
        typed.push_back(static_cast<char>(ch));
    }
    const auto isPrefix = [&](const char* word) {
        if (word == nullptr) return false;
        const std::string target(word);
        return typed.size() <= target.size() &&
               target.compare(0, typed.size(), typed) == 0;
    };
    return isPrefix(first) || isPrefix(second);
}

bool loopNode(NodeKind kind) {
    return kind == NodeKind::ForStatement ||
           kind == NodeKind::ForOfStatement ||
           kind == NodeKind::ForInStatement ||
           kind == NodeKind::WhileStatement ||
           kind == NodeKind::DoWhileStatement;
}

bool functionNode(NodeKind kind) {
    return kind == NodeKind::ArrowFunction ||
           kind == NodeKind::FunctionDeclaration ||
           kind == NodeKind::MethodDeclaration ||
           kind == NodeKind::ConstructorDeclaration ||
           kind == NodeKind::AccessorDeclaration;
}

const SwitchStatement* switchBodyStatementAt(const AnalyzedDocument& document,
                                             std::uint32_t offset) {
    if (!analysisReady(document)) return nullptr;
    const SwitchStatement* best = nullptr;
    forEachNode(*document.entry->ast, [&](const Node& node) {
        if (node.kind != NodeKind::SwitchStatement ||
            !node.span.valid() || node.span.file != document.file) {
            return;
        }
        const auto& statement = static_cast<const SwitchStatement&>(node);
        if (statement.discriminant == nullptr ||
            !statement.discriminant->span.valid() ||
            offset < statement.discriminant->span.end.offset) {
            return;
        }
        const std::size_t open = document.file->text.find(
            '{', statement.discriminant->span.end.offset);
        if (open == std::string::npos || offset <= open) return;
        for (const SwitchCasePtr& branch : statement.cases) {
            if (branch != nullptr && branch->span.valid() &&
                offset >= branch->span.begin.offset &&
                offset <= branch->span.end.offset) {
                return;
            }
        }
        if (best == nullptr || node.span.length() < best->span.length()) {
            best = &statement;
        }
    });
    return best;
}

bool switchBodyAt(const AnalyzedDocument& document, std::uint32_t offset) {
    return switchBodyStatementAt(document, offset) != nullptr;
}

bool tryAttachmentAt(const AnalyzedDocument& document, std::uint32_t offset,
                     const std::string& keyword) {
    if (!analysisReady(document) ||
        (keyword != "catch" && keyword != "finally")) {
        return false;
    }
    bool found = false;
    forEachNode(*document.entry->ast, [&](const Node& node) {
        if (found || node.kind != NodeKind::TryStatement ||
            !node.span.valid() || node.span.file != document.file) {
            return;
        }
        const auto& statement = static_cast<const TryStatement&>(node);
        if (statement.tryBlock == nullptr ||
            !statement.tryBlock->span.valid()) {
            return;
        }
        const std::uint32_t tryEnd = statement.tryBlock->span.end.offset;
        if (statement.handler == nullptr && offset >= tryEnd &&
            attachmentPrefixAt(document.file->text, tryEnd, offset,
                               "catch",
                               statement.finallyBlock ? nullptr : "finally")) {
            found = keyword == "catch" ? statement.handler == nullptr
                                        : statement.finallyBlock == nullptr;
            return;
        }
        if (statement.handler != nullptr &&
            statement.handler->span.valid() &&
            offset >= statement.handler->span.end.offset &&
            (statement.finallyBlock == nullptr ||
             (statement.finallyBlock->span.valid() &&
              offset <= statement.finallyBlock->span.begin.offset)) &&
            attachmentPrefixAt(document.file->text,
                               statement.handler->span.end.offset, offset,
                               statement.finallyBlock == nullptr ? "finally"
                                                                 : nullptr)) {
            found = keyword == "finally" && statement.finallyBlock == nullptr;
        }
    });
    return found;
}

bool elseAttachmentAt(const AnalyzedDocument& document, std::uint32_t offset) {
    if (!analysisReady(document)) return false;
    bool found = false;
    forEachNode(*document.entry->ast, [&](const Node& node) {
        if (found || node.kind != NodeKind::IfStatement ||
            !node.span.valid() || node.span.file != document.file) {
            return;
        }
        const auto& statement = static_cast<const IfStatement&>(node);
        if (statement.elseBranch != nullptr || statement.thenBranch == nullptr ||
            !statement.thenBranch->span.valid()) {
            return;
        }
        found = offset >= statement.thenBranch->span.end.offset &&
                attachmentPrefixAt(document.file->text,
                                   statement.thenBranch->span.end.offset, offset,
                                   "else");
    });
    return found;
}

}  // namespace

CompletionPosition classifyPosition(const AnalyzedDocument& document,
                                    const std::vector<Token>& tokens,
                                    const std::string& text,
                                    std::uint32_t offset) {
    const std::vector<OpenBracket> brackets = openBrackets(tokens, offset);
    const CompletionPosition position =
        tokenPosition(tokens, text, offset, brackets);
    // Parse/analyze can fail transiently while the user is halfway through a
    // declaration. Token context remains useful and keeps syntax completion
    // available even when no recoverable AST exists yet.
    if (!analysisReady(document)) return position;
    const std::vector<const Node*> chain = enclosingChain(document, offset);
    if (insideClassBody(chain, offset)) return CompletionPosition::ClassBody;
    if (tryAttachmentAt(document, offset, "catch") ||
        tryAttachmentAt(document, offset, "finally")) {
        return CompletionPosition::TryBlock;
    }
    if (switchBodyAt(document, offset)) {
        return CompletionPosition::SwitchBlock;
    }
    if (insideTypeNode(chain)) return CompletionPosition::TypePosition;
    // A `:` is a type annotation only where a declaration expects one.
    const Token* prev = previousToken(tokens, offset);
    if (prev != nullptr && prev->kind == TokenKind::Colon) {
        const Node* node = innermostOf(chain, NodeKind::Parameter);
        if (node == nullptr) node = innermostOf(chain, NodeKind::VariableDeclarator);
        if (node != nullptr && inAnnotationSlot(*node, offset)) {
            return CompletionPosition::TypePosition;
        }
    }
    return position;
}

bool awaitAllowedAt(const AnalyzedDocument& document, std::uint32_t offset) {
    if (!analysisReady(document)) return false;
    const std::vector<const Node*> chain = enclosingChain(document, offset);
    for (const Node* node : chain) {
        if (node->kind == NodeKind::Parameter) return false;
    }
    // Function boundaries shadow the surrounding context: a synchronous
    // callback nested inside an async function still cannot use `await`.
    for (std::size_t i = chain.size(); i-- > 0;) {
        const Node* node = chain[i];
        switch (node->kind) {
            case NodeKind::ArrowFunction:
                return static_cast<const ArrowFunction&>(*node).isAsync;
            case NodeKind::FunctionDeclaration:
                return static_cast<const FunctionDeclaration&>(*node).isAsync;
            case NodeKind::MethodDeclaration:
                return static_cast<const MethodDeclaration&>(*node).isAsync;
            case NodeKind::ConstructorDeclaration:
            case NodeKind::AccessorDeclaration:
                return false;
            default:
                break;
        }
    }
    return true;  // top-level await is supported by CP3.
}

bool contextualKeywordAllowed(const AnalyzedDocument& document,
                              std::uint32_t offset,
                              const std::string& keyword) {
    if (!analysisReady(document)) return false;
    const std::vector<const Node*> chain = enclosingChain(document, offset);
    if (keyword == "return") {
        for (std::size_t i = chain.size(); i-- > 0;) {
            if (functionNode(chain[i]->kind)) return true;
        }
        return false;
    }
    if (keyword == "break" || keyword == "continue") {
        for (std::size_t i = chain.size(); i-- > 0;) {
            const NodeKind kind = chain[i]->kind;
            if (functionNode(kind)) return false;
            if (loopNode(kind)) return true;
            if (keyword == "break" && kind == NodeKind::SwitchStatement) {
                return true;
            }
        }
        return false;
    }
    if (keyword == "this") {
        for (std::size_t i = chain.size(); i-- > 0;) {
            const NodeKind kind = chain[i]->kind;
            if (kind == NodeKind::ArrowFunction) continue;  // lexical this
            if (kind == NodeKind::MethodDeclaration ||
                kind == NodeKind::ConstructorDeclaration ||
                kind == NodeKind::AccessorDeclaration) {
                return true;
            }
            if (kind == NodeKind::FunctionDeclaration) return false;
        }
        return false;
    }
    if (keyword == "else") return elseAttachmentAt(document, offset);
    if (keyword == "catch" || keyword == "finally") {
        return tryAttachmentAt(document, offset, keyword);
    }
    if (keyword == "case" || keyword == "default") {
        const SwitchStatement* active =
            switchBodyStatementAt(document, offset);
        if (active == nullptr) return false;
        if (keyword == "case") return true;
        bool hasDefault = false;
        for (const SwitchCasePtr& branch : active->cases) {
            if (branch != nullptr && branch->isDefault) hasDefault = true;
        }
        return !hasDefault;
    }
    return true;
}

}  // namespace cp3::cli
