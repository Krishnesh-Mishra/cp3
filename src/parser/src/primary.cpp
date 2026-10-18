// =============================================================================
// src/parser/src/primary.cpp - primary and postfix (call/member) expressions.
//
// Implements the leaf expressions (literals, identifiers, template literals),
// array/object literals, `new`, calls and member/index access (including
// optional chaining and explicit call type arguments), and arrow functions.
// The operator-precedence chain lives in expressions.cpp.
// =============================================================================
#include "tokens.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

ExprPtr ParserImpl::parseCallMember() {
    const Token start = peek();
    ExprPtr expr = parsePrimary();
    if (!expr) return nullptr;
    return parseMemberAccess(std::move(expr), start);
}

ExprPtr ParserImpl::parseMemberAccess(ExprPtr expr, const Token& start) {
    for (;;) {
        if (match(TokenKind::Dot)) {
            Token propTok = peek();
            if (!isNameToken(propTok)) {
                report(diagcode::kUnexpectedToken, "expected a property name after '.'");
                break;
            }
            (void)advance();
            auto member = std::make_unique<MemberExpr>(expr->span);
            member->object = std::move(expr);
            member->property = textOf(propTok);
            member->optional = false;
            member->span = spanFrom(propTok);
            expr = std::move(member);
        } else if (check(TokenKind::QuestionDot)) {
            Token qd = advance();
            if (check(TokenKind::LParen)) {
                auto call = std::make_unique<CallExpr>(expr->span);
                call->callee = std::move(expr);
                call->optional = true;
                call->arguments = parseArguments();
                call->span = spanFrom(qd);
                expr = std::move(call);
            } else if (match(TokenKind::LBracket)) {
                auto index = std::make_unique<IndexExpr>(expr->span);
                index->object = std::move(expr);
                index->optional = true;
                index->index = parseExpression();
                (void)expect(TokenKind::RBracket, "expected ']' after index");
                index->span = spanFrom(qd);
                expr = std::move(index);
            } else {
                Token propTok = peek();
                if (!isNameToken(propTok)) {
                    report(diagcode::kUnexpectedToken, "expected a property name after '?.'");
                    break;
                }
                (void)advance();
                auto member = std::make_unique<MemberExpr>(expr->span);
                member->object = std::move(expr);
                member->property = textOf(propTok);
                member->optional = true;
                member->span = spanFrom(propTok);
                expr = std::move(member);
            }
        } else if (match(TokenKind::LBracket)) {
            Token bracket = prev();
            auto index = std::make_unique<IndexExpr>(expr->span);
            index->object = std::move(expr);
            index->optional = false;
            index->index = parseExpression();
            (void)expect(TokenKind::RBracket, "expected ']' after index");
            index->span = spanFrom(bracket);
            expr = std::move(index);
        } else if (check(TokenKind::LParen)) {
            auto call = std::make_unique<CallExpr>(expr->span);
            call->callee = std::move(expr);
            call->optional = false;
            call->arguments = parseArguments();
            call->span = spanFrom(start);
            expr = std::move(call);
        } else if (check(TokenKind::Less)) {
            TypeList typeArgs;
            if (!tryParseCallTypeArgs(typeArgs)) break;
            auto call = std::make_unique<CallExpr>(expr->span);
            call->callee = std::move(expr);
            call->typeArgs = std::move(typeArgs);
            call->arguments = parseArguments();
            call->span = spanFrom(start);
            expr = std::move(call);
        } else {
            break;
        }
    }
    return expr;
}

bool ParserImpl::tryParseCallTypeArgs(TypeList& out) {
    if (!check(TokenKind::Less)) return false;
    Trial trial(*this);
    (void)advance();  // <
    if (match(TokenKind::Greater)) {
        if (!check(TokenKind::LParen)) return false;
        trial.commit();
        return true;
    }
    do {
        TypePtr t = parseType();
        if (!t) return false;
        out.push_back(std::move(t));
        if (!match(TokenKind::Comma)) break;
    } while (!check(TokenKind::Greater) && !atEnd());
    if (!expectGreater()) return false;
    if (!check(TokenKind::LParen)) return false;
    trial.commit();
    return true;
}

ExprList ParserImpl::parseArguments() {
    ExprList out;
    if (!expect(TokenKind::LParen, "expected '('")) return out;
    while (!check(TokenKind::RParen) && !atEnd()) {
        if (match(TokenKind::Ellipsis)) {
            Token spreadStart = prev();
            auto spread = std::make_unique<SpreadElement>(spreadStart.span);
            spread->argument = parseAssignment();
            spread->span = spanFrom(spreadStart);
            out.push_back(std::move(spread));
        } else {
            ExprPtr e = parseAssignment();
            if (!e) break;
            out.push_back(std::move(e));
        }
        if (!match(TokenKind::Comma)) break;
    }
    (void)expect(TokenKind::RParen, "expected ')' after arguments");
    return out;
}

ExprPtr ParserImpl::parsePrimary() {
    const Token t = peek();
    if (ExprPtr lit = parsePrimaryLiteral(t)) return lit;

    switch (t.kind) {
        case TokenKind::KwNew:
            return parseNew();
        case TokenKind::LBracket:
            return parseArrayLiteral();
        case TokenKind::LBrace:
            return parseObjectLiteral();
        case TokenKind::LParen:
            return parseParenOrArrow();
        case TokenKind::KwAsync:
            return parseAsyncPrimary(t);
        default:
            break;
    }

    if (isIdentLike(t)) {
        if (peek(1).kind == TokenKind::Arrow && !peek(1).precededByNewline) {
            return parseSingleParamArrow(false);
        }
        (void)advance();
        return std::make_unique<AstIdentifier>(t.span, textOf(t));
    }

    report(diagcode::kUnexpectedToken, "expected an expression");
    return nullptr;
}

ExprPtr ParserImpl::parsePrimaryLiteral(const Token& t) {
    switch (t.kind) {
        case TokenKind::NumericLiteral:
            (void)advance();
            return std::make_unique<NumberLiteral>(t.span, t.numberValue, textOf(t));
        case TokenKind::StringLiteral:
            (void)advance();
            return std::make_unique<AstStringLiteral>(t.span, t.stringValue, textOf(t));
        case TokenKind::TemplateString: {
            (void)advance();
            auto tl = std::make_unique<TemplateLiteral>(t.span);
            auto quasi = std::make_unique<TemplateElement>(t.span);
            quasi->raw = t.rawText;
            quasi->cooked = t.stringValue;
            quasi->tail = true;
            tl->quasis.push_back(std::move(quasi));
            return tl;
        }
        case TokenKind::TemplateHead:
            return parseTemplateLiteral();
        case TokenKind::RegExpLiteral: {
            (void)advance();
            std::string pattern = t.stringValue;
            std::string flags = t.rawText;
            if (pattern.empty() && t.lexeme.size() >= 2 && t.lexeme.front() == '/') {
                std::string_view body = t.lexeme;
                std::size_t last = body.rfind('/');
                if (last != std::string_view::npos && last > 0) {
                    pattern = std::string(body.substr(1, last - 1));
                    flags = std::string(body.substr(last + 1));
                }
            }
            return std::make_unique<AstRegExpLiteral>(
                t.span, std::move(pattern), std::move(flags));
        }
        case TokenKind::KwTrue:
            (void)advance();
            return std::make_unique<BoolLiteral>(t.span, true);
        case TokenKind::KwFalse:
            (void)advance();
            return std::make_unique<BoolLiteral>(t.span, false);
        case TokenKind::KwNull:
            (void)advance();
            return std::make_unique<NullLiteral>(t.span);
        case TokenKind::KwUndefined:
            (void)advance();
            return std::make_unique<UndefinedLiteral>(t.span);
        case TokenKind::KwThis:
            (void)advance();
            return std::make_unique<ThisExpr>(t.span);
        default:
            return nullptr;
    }
}

// `async (a) => ...` / `async x => ...` vs. the identifier `async`.
ExprPtr ParserImpl::parseAsyncPrimary(const Token& t) {
    if (peek(1).kind == TokenKind::LParen) {
        Trial trial(*this);
        (void)advance();  // async
        ExprPtr arrow = parseParenOrArrow();
        if (arrow && arrow->kind == NodeKind::ArrowFunction) {
            static_cast<ArrowFunction&>(*arrow).isAsync = true;
            trial.commit();
            return arrow;
        }
        // fall through: treat as identifier `async`
    } else if (isIdentLike(peek(1)) && peek(2).kind == TokenKind::Arrow &&
               !peek(2).precededByNewline) {
        (void)advance();  // async
        return parseSingleParamArrow(true);
    }
    (void)advance();
    return std::make_unique<AstIdentifier>(t.span, textOf(t));
}

ExprPtr ParserImpl::parseNew() {
    const Token start = peek();
    (void)advance();  // new
    ExprPtr callee = parseNewCallee();
    auto expr = std::make_unique<NewExpr>(start.span);
    expr->callee = std::move(callee);
    if (check(TokenKind::Less)) {
        TypeList typeArgs;
        if (tryParseCallTypeArgs(typeArgs)) expr->typeArgs = std::move(typeArgs);
    }
    if (check(TokenKind::LParen)) expr->arguments = parseArguments();
    expr->span = spanFrom(start);
    return expr;
}

ExprPtr ParserImpl::parseNewCallee() {
    const Token start = peek();
    ExprPtr expr;
    if (check(TokenKind::KwThis)) {
        (void)advance();
        expr = std::make_unique<ThisExpr>(start.span);
    } else if (isIdentLike(start)) {
        (void)advance();
        expr = std::make_unique<AstIdentifier>(start.span, textOf(start));
    } else {
        report(diagcode::kUnexpectedToken, "expected a constructor name after 'new'");
        return nullptr;
    }
    while (check(TokenKind::Dot) || check(TokenKind::LBracket)) {
        if (match(TokenKind::Dot)) {
            Token propTok = peek();
            if (isNameToken(propTok)) {
                (void)advance();
                auto member = std::make_unique<MemberExpr>(expr->span);
                member->object = std::move(expr);
                member->property = textOf(propTok);
                member->span = spanFrom(propTok);
                expr = std::move(member);
            } else {
                report(diagcode::kUnexpectedToken, "expected a property name after '.'");
                break;
            }
        } else {
            Token bracket = advance();
            auto index = std::make_unique<IndexExpr>(expr->span);
            index->object = std::move(expr);
            index->index = parseExpression();
            (void)expect(TokenKind::RBracket, "expected ']' after index");
            index->span = spanFrom(bracket);
            expr = std::move(index);
        }
    }
    return expr;
}

ExprPtr ParserImpl::parseParenOrArrow() {
    const Token start = peek();
    {
        Trial trial(*this);
        (void)advance();  // (
        ParamList params;
        if (parseParamsBody(params)) {
            TypePtr returnType;
            if (match(TokenKind::Colon)) returnType = parseType();
            if (check(TokenKind::Arrow)) {
                trial.commit();
                (void)advance();  // =>
                auto fn = std::make_unique<ArrowFunction>(start.span);
                fn->params = std::move(params);
                fn->returnType = std::move(returnType);
                if (check(TokenKind::LBrace)) {
                    fn->body = parseBlock();
                    fn->expressionBody = false;
                } else {
                    fn->body = parseAssignment();
                    fn->expressionBody = true;
                }
                fn->span = spanFrom(start);
                return fn;
            }
        }
    }
    return parseGroupedExpression();
}

ExprPtr ParserImpl::parseGroupedExpression() {
    (void)expect(TokenKind::LParen, "expected '('");
    ExprPtr e = parseExpression();
    (void)expect(TokenKind::RParen, "expected ')'");
    return e;
}

ExprPtr ParserImpl::parseSingleParamArrow(bool isAsync) {
    const Token start = peek();
    auto param = std::make_unique<Parameter>(start.span);
    param->name = parseBindingTarget();
    if (match(TokenKind::Colon)) param->type = parseType();
    param->span = spanFrom(start);
    ParamList params;
    params.push_back(std::move(param));
    (void)expect(TokenKind::Arrow, "expected '=>'");
    auto fn = std::make_unique<ArrowFunction>(start.span);
    fn->isAsync = isAsync;
    fn->params = std::move(params);
    if (check(TokenKind::LBrace)) {
        fn->body = parseBlock();
        fn->expressionBody = false;
    } else {
        fn->body = parseAssignment();
        fn->expressionBody = true;
    }
    fn->span = spanFrom(start);
    return fn;
}

}  // namespace cp3
