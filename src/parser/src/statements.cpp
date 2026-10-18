// =============================================================================
// src/parser/src/statements.cpp - control-flow and simple statements.
//
// Implements the statement forms dispatched by ParserImpl::parseStatement()
// (parser.cpp): if/while/do-while/for/for-of/for-in/switch, break/continue/
// return/throw, try/catch/finally, labeled and expression statements.
// =============================================================================
#include "detail/tokens.h"

#include <memory>
#include <utility>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

StmtPtr ParserImpl::parseIfStatement() {
    const Token start = peek();
    (void)advance();
    (void)expect(TokenKind::LParen, "expected '(' after 'if'");
    auto stmt = std::make_unique<IfStatement>(start.span);
    stmt->condition = parseExpression();
    (void)expect(TokenKind::RParen, "expected ')' after if condition");
    stmt->thenBranch = parseStatement();
    if (!stmt->thenBranch) {
        stmt->thenBranch = std::make_unique<EmptyStatement>(prev().span);
    }
    if (match(TokenKind::KwElse)) {
        stmt->elseBranch = parseStatement();
        if (!stmt->elseBranch) {
            stmt->elseBranch = std::make_unique<EmptyStatement>(prev().span);
        }
    }
    stmt->span = spanFrom(start);
    return stmt;
}

StmtPtr ParserImpl::parseWhileStatement() {
    const Token start = peek();
    (void)advance();
    (void)expect(TokenKind::LParen, "expected '(' after 'while'");
    auto stmt = std::make_unique<WhileStatement>(start.span);
    stmt->condition = parseExpression();
    (void)expect(TokenKind::RParen, "expected ')' after while condition");
    stmt->body = parseStatement();
    if (!stmt->body) stmt->body = std::make_unique<EmptyStatement>(prev().span);
    stmt->span = spanFrom(start);
    return stmt;
}

StmtPtr ParserImpl::parseDoWhileStatement() {
    const Token start = peek();
    (void)advance();
    auto stmt = std::make_unique<DoWhileStatement>(start.span);
    stmt->body = parseStatement();
    if (!stmt->body) stmt->body = std::make_unique<EmptyStatement>(prev().span);
    if (!match(TokenKind::KwWhile)) {
        report(diagcode::kExpectedToken, "expected 'while' after 'do' body");
    }
    (void)expect(TokenKind::LParen, "expected '(' after 'while'");
    stmt->condition = parseExpression();
    (void)expect(TokenKind::RParen, "expected ')' after do..while condition");
    consumeSemicolon();
    stmt->span = spanFrom(start);
    return stmt;
}

StmtPtr ParserImpl::parseForStatement() {
    const Token start = peek();
    (void)advance();
    bool isAwait = false;
    if (match(TokenKind::KwAwait)) isAwait = true;
    (void)expect(TokenKind::LParen, "expected '(' after 'for'");

    NodePtr init;
    ExprPtr right;
    bool isForOf = false;
    bool isForIn = false;

    if (!check(TokenKind::Semicolon)) {
        if (check(TokenKind::KwLet) || check(TokenKind::KwVar) || check(TokenKind::KwConst)) {
            init = parseVariableDeclaration(false);
        } else {
            bool savedNoIn = noIn_;
            noIn_ = true;
            ExprPtr expr = parseExpression();
            noIn_ = savedNoIn;
            init = std::move(expr);
        }
        if (match(TokenKind::KwOf)) {
            isForOf = true;
            right = parseExpression();
        } else if (match(TokenKind::KwIn)) {
            isForIn = true;
            right = parseExpression();
        }
    }

    StmtPtr result;
    if (isForOf) {
        auto s = std::make_unique<ForOfStatement>(start.span);
        s->isAwait = isAwait;
        s->left = std::move(init);
        s->right = std::move(right);
        (void)expect(TokenKind::RParen, "expected ')' after for..of header");
        s->body = parseStatement();
        if (!s->body) s->body = std::make_unique<EmptyStatement>(prev().span);
        s->span = spanFrom(start);
        result = std::move(s);
    } else if (isForIn) {
        auto s = std::make_unique<ForInStatement>(start.span);
        s->left = std::move(init);
        s->right = std::move(right);
        (void)expect(TokenKind::RParen, "expected ')' after for..in header");
        s->body = parseStatement();
        if (!s->body) s->body = std::make_unique<EmptyStatement>(prev().span);
        s->span = spanFrom(start);
        result = std::move(s);
    } else {
        result = parseClassicFor(start, std::move(init));
    }
    return result;
}

// The C-style `for (init; cond; update) body` form, reached when the header is
// neither for-of nor for-in.
StmtPtr ParserImpl::parseClassicFor(const Token& start, NodePtr init) {
    auto s = std::make_unique<ForStatement>(start.span);
    s->init = std::move(init);
    (void)expect(TokenKind::Semicolon, "expected ';' in for header");
    if (!check(TokenKind::Semicolon)) s->condition = parseExpression();
    (void)expect(TokenKind::Semicolon, "expected ';' in for header");
    if (!check(TokenKind::RParen)) s->update = parseExpression();
    (void)expect(TokenKind::RParen, "expected ')' after for header");
    s->body = parseStatement();
    if (!s->body) s->body = std::make_unique<EmptyStatement>(prev().span);
    s->span = spanFrom(start);
    return s;
}

StmtPtr ParserImpl::parseSwitchStatement() {
    const Token start = peek();
    (void)advance();
    (void)expect(TokenKind::LParen, "expected '(' after 'switch'");
    auto stmt = std::make_unique<SwitchStatement>(start.span);
    stmt->discriminant = parseExpression();
    (void)expect(TokenKind::RParen, "expected ')' after switch discriminant");
    (void)expect(TokenKind::LBrace, "expected '{' to open switch body");

    while (!check(TokenKind::RBrace) && !atEnd()) {
        Token caseStart = peek();
        bool isDefault = false;
        ExprPtr test;
        if (match(TokenKind::KwCase)) {
            test = parseExpression();
        } else if (match(TokenKind::KwDefault)) {
            isDefault = true;
        } else {
            report(diagcode::kUnexpectedToken, "expected 'case' or 'default'");
            synchronize();
            if (check(TokenKind::RBrace)) break;
            continue;
        }
        (void)expect(TokenKind::Colon, "expected ':' after case label");

        auto c = std::make_unique<SwitchCase>(caseStart.span);
        c->isDefault = isDefault;
        c->test = std::move(test);
        while (!check(TokenKind::KwCase) && !check(TokenKind::KwDefault) &&
               !check(TokenKind::RBrace) && !atEnd()) {
            std::size_t before = ts_.cursor();
            StmtPtr s = parseStatement();
            if (s) {
                c->statements.push_back(std::move(s));
            } else {
                synchronize();
            }
            if (ts_.cursor() == before && !check(TokenKind::RBrace) && !atEnd()) {
                (void)advance();
            }
        }
        c->span = spanFrom(caseStart);
        stmt->cases.push_back(std::move(c));
    }
    (void)expect(TokenKind::RBrace, "expected '}' to close switch body");
    stmt->span = spanFrom(start);
    return stmt;
}

StmtPtr ParserImpl::parseBreakStatement() {
    const Token start = peek();
    (void)advance();
    auto stmt = std::make_unique<BreakStatement>(start.span);
    if (isIdentLike(peek()) && !peek().precededByNewline) {
        stmt->label = advanceText();
        stmt->hasLabel = true;
    }
    consumeSemicolon();
    stmt->span = spanFrom(start);
    return stmt;
}

StmtPtr ParserImpl::parseContinueStatement() {
    const Token start = peek();
    (void)advance();
    auto stmt = std::make_unique<ContinueStatement>(start.span);
    if (isIdentLike(peek()) && !peek().precededByNewline) {
        stmt->label = advanceText();
        stmt->hasLabel = true;
    }
    consumeSemicolon();
    stmt->span = spanFrom(start);
    return stmt;
}

StmtPtr ParserImpl::parseReturnStatement() {
    const Token start = peek();
    (void)advance();
    auto stmt = std::make_unique<ReturnStatement>(start.span);
    if (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !atEnd() &&
        !peek().precededByNewline) {
        stmt->argument = parseExpression();
    }
    consumeSemicolon();
    stmt->span = spanFrom(start);
    return stmt;
}

StmtPtr ParserImpl::parseThrowStatement() {
    const Token start = peek();
    (void)advance();
    auto stmt = std::make_unique<ThrowStatement>(start.span);
    if (!peek().precededByNewline && !check(TokenKind::Semicolon) &&
        !check(TokenKind::RBrace) && !atEnd()) {
        stmt->argument = parseExpression();
    } else {
        report(diagcode::kUnexpectedToken, "expected an expression after 'throw'");
    }
    consumeSemicolon();
    stmt->span = spanFrom(start);
    return stmt;
}

StmtPtr ParserImpl::parseTryStatement() {
    const Token start = peek();
    (void)advance();
    auto stmt = std::make_unique<TryStatement>(start.span);
    stmt->tryBlock = parseStatement();
    if (match(TokenKind::KwCatch)) {
        Token catchStart = prev();
        auto handler = std::make_unique<CatchClause>(catchStart.span);
        if (match(TokenKind::LParen)) {
            if (!check(TokenKind::RParen)) handler->param = parseBindingTarget();
            (void)expect(TokenKind::RParen, "expected ')' after catch parameter");
        }
        handler->body = parseStatement();
        if (!handler->body) handler->body = std::make_unique<EmptyStatement>(prev().span);
        handler->span = spanFrom(catchStart);
        stmt->handler = std::move(handler);
    }
    if (match(TokenKind::KwFinally)) {
        stmt->finallyBlock = parseStatement();
        if (!stmt->finallyBlock) {
            stmt->finallyBlock = std::make_unique<EmptyStatement>(prev().span);
        }
    }
    stmt->span = spanFrom(start);
    return stmt;
}

StmtPtr ParserImpl::parseLabeledStatement() {
    const Token start = peek();
    auto stmt = std::make_unique<LabeledStatement>(start.span);
    stmt->label = advanceText();
    (void)expect(TokenKind::Colon, "expected ':' after label");
    stmt->body = parseStatement();
    if (!stmt->body) stmt->body = std::make_unique<EmptyStatement>(prev().span);
    stmt->span = spanFrom(start);
    return stmt;
}

StmtPtr ParserImpl::parseExpressionStatement() {
    const Token start = peek();
    ExprPtr expr = parseExpression();
    if (!expr) return nullptr;
    consumeSemicolon();
    auto stmt = std::make_unique<ExpressionStatement>(start.span);
    stmt->expression = std::move(expr);
    stmt->span = spanFrom(start);
    return stmt;
}

}  // namespace cp3
