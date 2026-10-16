// =============================================================================
// src/parser/src/members.cpp — class and interface members.
//
// Parses class-body members (methods, accessors, constructors and properties,
// with their public/private/protected/static/abstract/weak/readonly modifiers)
// and interface / object-type signature members (property and method
// signatures, including the banned index signature).
// =============================================================================
#include "tokens.h"

#include <memory>
#include <utility>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// Class / interface members
// =============================================================================
bool ParserImpl::looksLikeReadonlyModifier() const {
    if (!(check(TokenKind::Identifier) && textOf(peek()) == "readonly")) return false;
    TokenKind n = peek(1).kind;
    if (n == TokenKind::Colon || n == TokenKind::Question || n == TokenKind::LParen ||
        n == TokenKind::Assign || n == TokenKind::Semicolon || n == TokenKind::Comma) {
        return false;
    }
    return n == TokenKind::Identifier || n == TokenKind::StringLiteral ||
           n == TokenKind::NumericLiteral || n == TokenKind::LBracket ||
           n == TokenKind::KwStatic || n == TokenKind::KwPublic ||
           n == TokenKind::KwPrivate || n == TokenKind::KwProtected ||
           n == TokenKind::KwFunction || n == TokenKind::KwGet || n == TokenKind::KwSet;
}

bool ParserImpl::looksLikeAccessor() const {
    TokenKind k = peek().kind;
    if (k != TokenKind::KwGet && k != TokenKind::KwSet) return false;
    TokenKind n = peek(1).kind;
    if (n == TokenKind::LBracket) return true;
    if (n == TokenKind::Identifier || n == TokenKind::StringLiteral ||
        n == TokenKind::NumericLiteral || isContextualKeyword(n)) {
        return peek(2).kind == TokenKind::LParen;
    }
    return false;
}

bool ParserImpl::looksLikeConstructor() const {
    return check(TokenKind::Identifier) && textOf(peek()) == "constructor" &&
           peek(1).kind == TokenKind::LParen;
}

ExprPtr ParserImpl::parseMemberKey(bool& computed) {
    computed = false;
    if (match(TokenKind::LBracket)) {
        computed = true;
        ExprPtr key = parseAssignment();
        (void)expect(TokenKind::RBracket, "expected ']' after computed key");
        return key;
    }
    ExprPtr key = parsePropertyKey();
    if (!key) report(diagcode::kUnexpectedToken, "expected a member name");
    return key;
}

ExprPtr ParserImpl::parsePropertyKey() {
    Token t = peek();
    if (t.kind == TokenKind::StringLiteral) {
        (void)advance();
        return std::make_unique<AstStringLiteral>(t.span, t.stringValue, textOf(t));
    }
    if (t.kind == TokenKind::NumericLiteral) {
        (void)advance();
        return std::make_unique<NumberLiteral>(t.span, t.numberValue, textOf(t));
    }
    if (isNameToken(t)) {
        (void)advance();
        return std::make_unique<AstIdentifier>(t.span, textOf(t));
    }
    return nullptr;
}

void ParserImpl::parseMemberModifiers(MemberModifiers& mods) {
    for (;;) {
        if (match(TokenKind::KwPublic)) mods.access = AccessModifier::Public;
        else if (match(TokenKind::KwPrivate)) mods.access = AccessModifier::Private;
        else if (match(TokenKind::KwProtected)) mods.access = AccessModifier::Protected;
        else if (match(TokenKind::KwStatic)) mods.isStatic = true;
        else if (match(TokenKind::KwAbstract)) mods.isAbstract = true;
        else if (match(TokenKind::KwWeak)) mods.isWeak = true;
        else if (looksLikeReadonlyModifier()) { mods.isReadonly = true; (void)advance(); }
        else break;
    }
    if (check(TokenKind::KwAsync) && peek(1).kind == TokenKind::KwFunction) {
        mods.isAsync = true;
        (void)advance();
    }
}

ClassMemberPtr ParserImpl::parseClassMember() {
    MemberModifiers mods;
    mods.start = peek();
    parseMemberModifiers(mods);

    if (match(TokenKind::KwFunction)) {
        if (check(TokenKind::Star)) {
            report(diagcode::kUnexpectedToken, "generators are not supported");
            (void)advance();
        }
        return parseMethodMember(mods);
    }
    if (looksLikeAccessor()) {
        (void)advance();
        return parseAccessorMember(mods);
    }
    if (looksLikeConstructor()) {
        (void)advance();
        return parseConstructorMember(mods);
    }
    return parsePropertyMember(mods);
}

ClassMemberPtr ParserImpl::parseMethodMember(MemberModifiers& mods) {
    auto method = std::make_unique<MethodDeclaration>(mods.start.span);
    method->access = mods.access;
    method->isStatic = mods.isStatic;
    method->isAbstract = mods.isAbstract;
    method->isAsync = mods.isAsync;
    method->isReadonly = mods.isReadonly;
    method->key = parseMemberKey(method->computed);
    if (match(TokenKind::Question)) method->optional = true;
    method->typeParams = parseTypeParamsIfPresent();
    method->params = parseParamList();
    if (match(TokenKind::Colon)) method->returnType = parseType();
    if (check(TokenKind::LBrace)) {
        method->body = parseBlock();
    } else {
        consumeSemicolon();
    }
    method->span = spanFrom(mods.start);
    return method;
}

ClassMemberPtr ParserImpl::parseAccessorMember(MemberModifiers& mods) {
    AccessorKind kind =
        prev().kind == TokenKind::KwGet ? AccessorKind::Get : AccessorKind::Set;
    auto acc = std::make_unique<AccessorDeclaration>(mods.start.span);
    acc->accessor = kind;
    acc->access = mods.access;
    acc->isStatic = mods.isStatic;
    acc->key = parseMemberKey(acc->computed);
    acc->params = parseParamList();
    if (match(TokenKind::Colon)) acc->returnType = parseType();
    if (check(TokenKind::LBrace)) {
        acc->body = parseBlock();
    } else {
        consumeSemicolon();
    }
    acc->span = spanFrom(mods.start);
    return acc;
}

ClassMemberPtr ParserImpl::parseConstructorMember(MemberModifiers& mods) {
    auto ctor = std::make_unique<ConstructorDeclaration>(mods.start.span);
    ctor->access = mods.access;
    ctor->params = parseParamList();
    if (check(TokenKind::LBrace)) {
        ctor->body = parseBlock();
    } else {
        consumeSemicolon();
    }
    ctor->span = spanFrom(mods.start);
    return ctor;
}

ClassMemberPtr ParserImpl::parsePropertyMember(const MemberModifiers& mods) {
    auto prop = std::make_unique<PropertyDeclaration>(mods.start.span);
    prop->access = mods.access;
    prop->isStatic = mods.isStatic;
    prop->isAbstract = mods.isAbstract;
    prop->isWeak = mods.isWeak;
    prop->isReadonly = mods.isReadonly;
    prop->key = parseMemberKey(prop->computed);
    if (!prop->key) report(diagcode::kUnexpectedToken, "expected a class member name");
    if (match(TokenKind::Question)) prop->optional = true;
    if (match(TokenKind::Colon)) prop->type = parseType();
    if (match(TokenKind::Assign)) prop->initializer = parseAssignment();
    consumeSemicolon();
    prop->span = spanFrom(mods.start);
    return prop;
}

SignatureMemberPtr ParserImpl::parseSignatureMember() {
    const Token start = peek();
    bool isReadonly = false;
    if (looksLikeReadonlyModifier()) {
        isReadonly = true;
        (void)advance();
    }

    // Index signatures have no AST node and are banned (CP3803 / spec §8).
    if (check(TokenKind::LBracket)) {
        report(diagcode::kUnexpectedToken,
               "index signatures are not supported; use Map<K, V>");
        int depth = 0;
        while (!atEnd()) {
            Token t = advance();
            if (t.kind == TokenKind::LBracket) ++depth;
            else if (t.kind == TokenKind::RBracket && --depth == 0) break;
        }
        if (match(TokenKind::Colon)) (void)parseType();  // skip the value type
        return nullptr;
    }

    bool hasFunction = false;
    if (match(TokenKind::KwFunction)) {
        hasFunction = true;
    } else if (check(TokenKind::KwAsync) && peek(1).kind == TokenKind::KwFunction) {
        hasFunction = true;
        (void)advance();
    }

    bool computed = false;
    ExprPtr key = parseMemberKey(computed);
    bool optional = match(TokenKind::Question);

    if (check(TokenKind::LParen) || check(TokenKind::Less)) {
        auto method = std::make_unique<MethodSignature>(start.span);
        method->key = std::move(key);
        method->computed = computed;
        method->optional = optional;
        method->hasFunctionKeyword = hasFunction;
        method->typeParams = parseTypeParamsIfPresent();
        method->params = parseParamList();
        if (match(TokenKind::Colon)) method->returnType = parseType();
        method->span = spanFrom(start);
        return method;
    }

    auto prop = std::make_unique<PropertySignature>(start.span);
    prop->key = std::move(key);
    prop->computed = computed;
    prop->optional = optional;
    prop->isReadonly = isReadonly;
    if (match(TokenKind::Colon)) prop->type = parseType();
    prop->span = spanFrom(start);
    return prop;
}

}  // namespace cp3
