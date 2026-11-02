// =============================================================================
// src/parser/src/parser.cpp - parser entry point and statement dispatch.
//
// Owns the frozen `Parser::parseProgram()` boundary (cp3/parser.hpp), the
// top-level `ParserImpl::run()` loop, module-level statement dispatch
// (`parseStatement` / `parseDeclaration` / `parseBlock`) and the import/export
// declarations. Statements live in statements.cpp, declarations in
// declarations.cpp, and so on (STYLE.md §8).
// =============================================================================
#include "detail/tokens.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// Parser entry point (frozen boundary)
// =============================================================================
ProgramPtr Parser::parseProgram() {
    ParserImpl impl(tokens_, sink_);
    return impl.run();
}

// =============================================================================
// ParserImpl - module / statements
// =============================================================================
ProgramPtr ParserImpl::run() {
    SourceSpan programSpan = peek().span;
    auto program = std::make_unique<Program>(programSpan);

    while (!atEnd()) {
        std::size_t before = ts_.cursor();
        AnchorScope anchor(*this, peek());
        StmtPtr stmt = parseStatement();
        if (stmt) {
            program->body.push_back(std::move(stmt));
        } else {
            synchronize();
        }
        if (ts_.cursor() == before && !atEnd()) {
            (void)advance();
        }
    }

    const auto& toks = ts_.tokens();
    if (!toks.empty()) {
        program->span = SourceSpan::merge(toks.front().span, toks.back().span);
    }

    assignIds(*program);
    return program;
}

StmtPtr ParserImpl::parseStatement() {
    AnchorScope anchor(*this, peek());
    switch (peek().kind) {
        case TokenKind::LBrace:
            return parseBlock();
        case TokenKind::Semicolon: {
            Token t = advance();
            return std::make_unique<EmptyStatement>(t.span);
        }
        case TokenKind::KwIf:
            return parseIfStatement();
        case TokenKind::KwWhile:
            return parseWhileStatement();
        case TokenKind::KwDo:
            return parseDoWhileStatement();
        case TokenKind::KwFor:
            return parseForStatement();
        case TokenKind::KwSwitch:
            return parseSwitchStatement();
        case TokenKind::KwBreak:
            return parseBreakStatement();
        case TokenKind::KwContinue:
            return parseContinueStatement();
        case TokenKind::KwReturn:
            return parseReturnStatement();
        case TokenKind::KwThrow:
            return parseThrowStatement();
        case TokenKind::KwTry:
            return parseTryStatement();
        case TokenKind::KwImport:
            return parseImportDeclaration();
        case TokenKind::KwExport:
            return parseExportDeclaration();
        case TokenKind::At:
            return parseDecoratedStatement();
        default:
            break;
    }

    if (StmtPtr decl = parseDeclaration(false)) {
        return decl;
    }

    // Labeled statement: `name :` at statement start.
    if (isIdentLike(peek()) && peek(1).kind == TokenKind::Colon) {
        return parseLabeledStatement();
    }

    return parseExpressionStatement();
}

StmtPtr ParserImpl::parseDeclaration(bool isExported) {
    if (check(TokenKind::KwFunction) ||
        (check(TokenKind::KwAsync) && peek(1).kind == TokenKind::KwFunction)) {
        return parseFunctionDeclaration(isExported);
    }
    if (check(TokenKind::KwClass) ||
        (check(TokenKind::KwAbstract) && peek(1).kind == TokenKind::KwClass)) {
        return parseClassDeclaration(isExported);
    }
    if (check(TokenKind::KwInterface)) return parseInterfaceDeclaration(isExported);
    if (check(TokenKind::KwEnum)) return parseEnumDeclaration(isExported);
    if (check(TokenKind::KwType) && isIdentLike(peek(1))) return parseTypeAlias(isExported);
    if (check(TokenKind::KwLet) || check(TokenKind::KwVar) || check(TokenKind::KwConst)) {
        return parseVariableDeclaration(true);
    }
    return nullptr;
}

StmtPtr ParserImpl::parseBlock() {
    const Token start = peek();
    (void)expect(TokenKind::LBrace, "expected '{'");
    auto block = std::make_unique<BlockStatement>(start.span);
    while (!check(TokenKind::RBrace) && !atEnd()) {
        std::size_t before = ts_.cursor();
        StmtPtr stmt = parseStatement();
        if (stmt) {
            block->body.push_back(std::move(stmt));
        } else {
            synchronize();
        }
        if (ts_.cursor() == before && !check(TokenKind::RBrace) && !atEnd()) {
            (void)advance();
        }
    }
    (void)expect(TokenKind::RBrace, "expected '}'");
    block->span = spanFrom(start);
    return block;
}

// `@Name class ...`: decorators are compile-time macros. The parser keeps the
// decorator expressions on the class node; sema resolves each name to a
// `comptime function` and the C++ output never carries decorator metadata.
StmtPtr ParserImpl::parseDecoratedStatement() {
    const Token start = peek();
    std::vector<ExprPtr> decorators;
    parseDecorators(decorators);

    if (check(TokenKind::KwClass) ||
        (check(TokenKind::KwAbstract) && peek(1).kind == TokenKind::KwClass)) {
        StmtPtr stmt = parseClassDeclaration(false);
        if (stmt && stmt->kind == NodeKind::ClassDeclaration) {
            static_cast<ClassDeclaration&>(*stmt).decorators =
                std::move(decorators);
        }
        return stmt;
    }

    report(diagcode::kDecoratorsUnsupported,
           "TypeScript allows runtime decorators on declarations and values; "
           "CP3 does not run code while a class is being defined and emits no "
           "runtime metadata, so decorators are compile-time macros only. "
           "Attach `@Name` to a class or class member, where `Name` names a "
           "`comptime function` that rewrites the AST before native codegen");
    while (!atEnd() && !check(TokenKind::Semicolon) &&
           !check(TokenKind::RBrace)) {
        (void)advance();
    }
    if (match(TokenKind::Semicolon)) {
        // consumed
    }
    return std::make_unique<EmptyStatement>(spanFrom(start));
}

// =============================================================================
// Imports / exports
// =============================================================================
void ParserImpl::parseImportSpecifiers(ImportDeclaration& decl) {
    while (!check(TokenKind::RBrace) && !atEnd()) {
        Token specStart = peek();
        auto spec = std::make_unique<ImportSpecifier>(specStart.span);
        spec->imported = expectName("an imported name");
        spec->local = spec->imported;
        if (match(TokenKind::KwAs)) spec->local = expectName("a local name");
        spec->span = spanFrom(specStart);
        decl.specifiers.push_back(std::move(spec));
        if (!match(TokenKind::Comma)) break;
    }
    (void)expect(TokenKind::RBrace, "expected '}' to close named imports");
}

StmtPtr ParserImpl::parseImportDeclaration() {
    const Token start = peek();
    (void)expect(TokenKind::KwImport, "expected 'import'");
    auto decl = std::make_unique<ImportDeclaration>(start.span);

    if (match(TokenKind::KwNative)) {
        decl->kind = ImportKind::Native;
        if (check(TokenKind::StringLiteral)) {
            Token src = advance();
            decl->source = src.stringValue;
        } else {
            report(diagcode::kUnexpectedToken, "expected a module specifier string");
        }
        if (match(TokenKind::KwAs)) decl->alias = expectName("an alias");
        consumeSemicolon();
        decl->span = spanFrom(start);
        return decl;
    }

    if (check(TokenKind::StringLiteral)) {
        Token src = advance();
        decl->source = src.stringValue;
        if (match(TokenKind::KwAs)) {
            decl->kind = ImportKind::Cpp;
            decl->alias = expectName("an alias");
        } else {
            decl->kind = ImportKind::SideEffect;
        }
        consumeSemicolon();
        decl->span = spanFrom(start);
        return decl;
    }

    if (match(TokenKind::Star)) {
        decl->kind = ImportKind::Namespace;
        (void)expect(TokenKind::KwAs, "expected 'as' in namespace import");
        decl->namespaceBinding = expectName("a namespace binding");
    } else if (match(TokenKind::LBrace)) {
        decl->kind = ImportKind::Named;
        parseImportSpecifiers(*decl);
    } else if (isIdentLike(peek())) {
        decl->kind = ImportKind::Default;
        decl->defaultBinding = advanceText();
        if (match(TokenKind::Comma)) {
            if (match(TokenKind::Star)) {
                (void)expect(TokenKind::KwAs, "expected 'as' in namespace import");
                decl->namespaceBinding = expectName("a namespace binding");
                decl->kind = ImportKind::Namespace;
            } else if (match(TokenKind::LBrace)) {
                parseImportSpecifiers(*decl);
                decl->kind = ImportKind::Named;
            }
        }
    } else {
        report(diagcode::kUnexpectedToken, "expected an import clause");
    }

    (void)expect(TokenKind::KwFrom, "expected 'from'");
    if (check(TokenKind::StringLiteral)) {
        Token src = advance();
        decl->source = src.stringValue;
    } else {
        report(diagcode::kUnexpectedToken, "expected a module specifier string");
    }
    consumeSemicolon();
    decl->span = spanFrom(start);
    return decl;
}

StmtPtr ParserImpl::parseExportDeclaration() {
    const Token start = peek();
    (void)expect(TokenKind::KwExport, "expected 'export'");
    auto decl = std::make_unique<ExportDeclaration>(start.span);

    if (match(TokenKind::KwDefault)) {
        decl->kind = ExportKind::Default;
        if (check(TokenKind::KwFunction) ||
            (check(TokenKind::KwAsync) && peek(1).kind == TokenKind::KwFunction)) {
            decl->declaration = parseFunctionDeclaration(true);
        } else if (check(TokenKind::KwClass) ||
                   (check(TokenKind::KwAbstract) && peek(1).kind == TokenKind::KwClass)) {
            decl->declaration = parseClassDeclaration(true);
        } else {
            decl->defaultExpr = parseAssignment();
            consumeSemicolon();
        }
        decl->span = spanFrom(start);
        return decl;
    }

    if (match(TokenKind::Star)) {
        decl->kind = ExportKind::ReExportAll;
        if (match(TokenKind::KwAs)) decl->namespaceExport = expectName("an exported name");
        (void)expect(TokenKind::KwFrom, "expected 'from' in re-export");
        if (check(TokenKind::StringLiteral)) {
            Token src = advance();
            decl->source = src.stringValue;
        } else {
            report(diagcode::kUnexpectedToken, "expected a module specifier string");
        }
        consumeSemicolon();
        decl->span = spanFrom(start);
        return decl;
    }

    if (match(TokenKind::LBrace)) {
        decl->kind = ExportKind::Named;
        while (!check(TokenKind::RBrace) && !atEnd()) {
            Token specStart = peek();
            auto spec = std::make_unique<ExportSpecifier>(specStart.span);
            spec->local = expectName("an exported name");
            spec->exported = spec->local;
            if (match(TokenKind::KwAs)) spec->exported = expectName("an exported name");
            spec->span = spanFrom(specStart);
            decl->specifiers.push_back(std::move(spec));
            if (!match(TokenKind::Comma)) break;
        }
        (void)expect(TokenKind::RBrace, "expected '}' to close export list");
        if (match(TokenKind::KwFrom)) {
            decl->kind = ExportKind::ReExportNamed;
            if (check(TokenKind::StringLiteral)) {
                Token src = advance();
                decl->source = src.stringValue;
            } else {
                report(diagcode::kUnexpectedToken, "expected a module specifier string");
            }
        }
        consumeSemicolon();
        decl->span = spanFrom(start);
        return decl;
    }

    // export <declaration>
    decl->kind = ExportKind::Declaration;
    StmtPtr inner = parseDeclaration(true);
    if (!inner) {
        report(diagcode::kExpectedDeclaration, "expected a declaration after 'export'");
        decl->span = spanFrom(start);
        return decl;
    }
    decl->declaration = std::move(inner);
    decl->span = spanFrom(start);
    return decl;
}

}  // namespace cp3
