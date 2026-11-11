// =============================================================================
// src/parser/src/members.cpp - class and interface members.
//
// Parses class-body members (methods, accessors, constructors, static blocks
// and properties, with their TypeScript modifiers) and interface / object-type
// signature members (property and method signatures, including the banned
// index signature).
// =============================================================================
#include "detail/tokens.h"

#include <memory>
#include <utility>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// Decorators
// =============================================================================
void ParserImpl::parseDecorators(std::vector<ExprPtr>& out) {
    while (check(TokenKind::At)) {
        Token at = advance();
        ExprPtr expr = parseAssignment();
        if (!expr) {
            report(diagcode::kUnexpectedToken,
                   "expected a decorator expression after '@'");
            return;
        }
        expr->span = spanFrom(at);
        out.push_back(std::move(expr));
    }
}

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
    return n == TokenKind::Identifier || n == TokenKind::PrivateIdentifier ||
           n == TokenKind::StringLiteral || n == TokenKind::NumericLiteral ||
           n == TokenKind::LBracket || n == TokenKind::KwStatic ||
           n == TokenKind::KwPublic || n == TokenKind::KwPrivate ||
           n == TokenKind::KwProtected || n == TokenKind::KwFunction ||
           n == TokenKind::KwGet || n == TokenKind::KwSet;
}

bool ParserImpl::looksLikeOverrideModifier() const {
    if (!(check(TokenKind::Identifier) && textOf(peek()) == "override")) return false;
    TokenKind n = peek(1).kind;
    // `override: number` / `override()` are ordinary member names. Only a
    // following member name (or modifier) makes `override` a modifier.
    return n == TokenKind::Identifier || n == TokenKind::PrivateIdentifier ||
           n == TokenKind::StringLiteral || n == TokenKind::NumericLiteral ||
           n == TokenKind::LBracket || n == TokenKind::KwStatic ||
           n == TokenKind::KwPublic || n == TokenKind::KwPrivate ||
           n == TokenKind::KwProtected || n == TokenKind::KwGet ||
           n == TokenKind::KwSet || n == TokenKind::KwAsync;
}

bool ParserImpl::looksLikeAccessor() const {
    TokenKind k = peek().kind;
    if (k != TokenKind::KwGet && k != TokenKind::KwSet) return false;
    TokenKind n = peek(1).kind;
    if (n == TokenKind::LBracket) return true;
    if (n == TokenKind::Identifier || n == TokenKind::PrivateIdentifier ||
        n == TokenKind::StringLiteral || n == TokenKind::NumericLiteral ||
        isContextualKeyword(n)) {
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
    if (t.kind == TokenKind::PrivateIdentifier) {
        (void)advance();
        return std::make_unique<AstIdentifier>(t.span, textOf(t));
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
        else if (match(TokenKind::KwWeak)) {
            // Manual escape hatch for reference cycles (automatic cycle
            // handling task): a weak field does not count toward the
            // refcount and reads as `T | null`.
            mods.isWeak = true;
        } else if (looksLikeReadonlyModifier()) {
            mods.isReadonly = true;
            (void)advance();
        } else if (looksLikeOverrideModifier()) {
            mods.isOverride = true;
            (void)advance();
        } else {
            break;
        }
    }
    // `async greet() {}`: a modifier only when a member name follows. A member
    // literally named `async` (`async() {}`) is not a modifier.
    if (check(TokenKind::KwAsync) && peek(1).kind != TokenKind::LParen &&
        peek(1).kind != TokenKind::Colon && peek(1).kind != TokenKind::Question) {
        mods.isAsync = true;
        (void)advance();
    }
}

ClassMemberPtr ParserImpl::parseStaticBlock(MemberModifiers& mods) {
    Token start = mods.start;
    auto block = std::make_unique<StaticBlock>(start.span);
    block->decorators = std::move(mods.decorators);
    block->body = parseBlock();
    block->span = spanFrom(start);
    return block;
}

ClassMemberPtr ParserImpl::parseClassMember() {
    MemberModifiers mods;
    mods.start = peek();
    parseDecorators(mods.decorators);
    parseMemberModifiers(mods);

    if (mods.isStatic && check(TokenKind::LBrace)) {
        return parseStaticBlock(mods);
    }
    if (match(TokenKind::KwFunction)) {
        // Legacy `function method(...) {}` spelling: accepted (doc comments
        // in the stdlib corpus use it); TypeScript `method(...) {}` is the
        // preferred form, both parse as the same MethodDeclaration.
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

    bool computed = false;
    ExprPtr key = parseMemberKey(computed);
    if (check(TokenKind::LParen) || check(TokenKind::Less)) {
        return parseMethodMember(mods, std::move(key), computed);
    }
    return parsePropertyMember(mods, std::move(key), computed);
}

ClassMemberPtr ParserImpl::parseMethodMember(MemberModifiers& mods,
                                             ExprPtr key, bool computed) {
    auto method = std::make_unique<MethodDeclaration>(mods.start.span);
    method->decorators = std::move(mods.decorators);
    method->access = mods.access;
    method->isStatic = mods.isStatic;
    method->isAbstract = mods.isAbstract;
    method->isAsync = mods.isAsync;
    method->isReadonly = mods.isReadonly;
    method->isOverride = mods.isOverride;
    method->key = key ? std::move(key) : parseMemberKey(method->computed);
    if (computed) method->computed = true;
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
    acc->decorators = std::move(mods.decorators);
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
    ctor->decorators = std::move(mods.decorators);
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

ClassMemberPtr ParserImpl::parsePropertyMember(MemberModifiers& mods,
                                               ExprPtr key, bool computed) {
    auto prop = std::make_unique<PropertyDeclaration>(mods.start.span);
    prop->decorators = std::move(mods.decorators);
    prop->access = mods.access;
    prop->isStatic = mods.isStatic;
    prop->isAbstract = mods.isAbstract;
    prop->isWeak = mods.isWeak;
    prop->isReadonly = mods.isReadonly;
    prop->isOverride = mods.isOverride;
    prop->key = key ? std::move(key) : parseMemberKey(prop->computed);
    if (computed) prop->computed = true;
    if (!prop->key) report(diagcode::kUnexpectedToken, "expected a class member name");
    if (match(TokenKind::Question)) prop->optional = true;
    if (check(TokenKind::Bang)) {
        advance();
        prop->isDefiniteAssignment = true;
    }
    if (match(TokenKind::Colon)) prop->type = parseType();
    if (match(TokenKind::Assign)) prop->initializer = parseAssignment();
    consumeSemicolon();
    prop->span = spanFrom(mods.start);
    return prop;
}

// Index signatures (spec §8, v0.2.4): `[name: string]: T` -- an open key set
// with a single value type. Only `string` and `number` keys are accepted;
// anything else reports and falls back to `string` (the grammar is pinned so a
// misspelling flags instead of silently accepting a nonsense key).
SignatureMemberPtr ParserImpl::parseIndexSignature(Token start,
                                                   bool isReadonly) {
    (void)advance();  // '['
    std::string name;
    if (isIdentLike(peek())) {
        name = textOf(advance());
    } else {
        report(diagcode::kExpectedToken,
               "expected a name in the index signature");
        name = "x";
    }
    (void)expect(TokenKind::Colon, "expected ':' after the index signature name");
    // The key type: exactly `string` or `number`.
    auto key = std::make_unique<NamedType>(spanFrom(peek()));
    if (isIdentLike(peek())) {
        const std::string keyName = textOf(peek());
        if (keyName == "string" || keyName == "number") {
            key->name = keyName;
        } else {
            report(diagcode::kUnexpectedToken,
                   "an index signature key must be 'string' or 'number'");
            key->name = "string";
        }
        key->span = peek().span;
        (void)advance();
    } else {
        report(diagcode::kExpectedToken,
               "expected 'string' or 'number' as the index signature key");
        key->name = "string";
        key->span = peek().span;
    }
    if (check(TokenKind::RBracket)) {
        (void)advance();
    } else {
        report(diagcode::kExpectedToken,
               "expected ']' after the index signature key");
        // Recover: skip to the closing bracket (TS recovery shape).
        int depth = 0;
        while (!atEnd()) {
            Token t = advance();
            if (t.kind == TokenKind::LBracket) ++depth;
            else if (t.kind == TokenKind::RBracket && --depth == 0) break;
        }
    }
    (void)expect(TokenKind::Colon, "expected ':' after the index signature");

    auto index = std::make_unique<IndexSignature>(start.span);
    index->name = std::move(name);
    index->isReadonly = isReadonly;
    index->keyType = std::move(key);
    index->valueType = parseType();
    return index;
}

SignatureMemberPtr ParserImpl::parseSignatureMember() {
    const Token start = peek();
    bool isReadonly = false;
    if (looksLikeReadonlyModifier()) {
        isReadonly = true;
        (void)advance();
    }

    // Index signatures (spec 8, v0.2.4): `[name: string]: T` -- an open key set.
    if (check(TokenKind::LBracket)) {
        return parseIndexSignature(start, isReadonly);
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
