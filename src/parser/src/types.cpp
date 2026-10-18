// =============================================================================
// src/parser/src/types.cpp - type syntax.
//
// Implements the type grammar: union types, postfix array types, and primary
// types (parenthesized / function types, tuples, object type literals, literal
// types, prefixed escape-hatch types `r_'u32'`, and named/generic types).
// =============================================================================
#include "tokens.h"

#include <memory>
#include <string>
#include <utility>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

TypePtr ParserImpl::parseType() { return parseUnionType(); }

TypePtr ParserImpl::parseUnionType() {
    const Token start = peek();
    TypePtr first = parsePostfixType();
    if (!first) return nullptr;
    if (!check(TokenKind::Pipe)) return first;

    auto uni = std::make_unique<UnionType>(start.span);
    uni->types.push_back(std::move(first));
    while (match(TokenKind::Pipe)) {
        TypePtr t = parsePostfixType();
        if (!t) break;
        uni->types.push_back(std::move(t));
    }
    uni->span = spanFrom(start);
    return uni;
}

TypePtr ParserImpl::parsePostfixType() {
    const Token start = peek();
    TypePtr base = parsePrimaryType();
    if (!base) return nullptr;
    while (check(TokenKind::LBracket)) {
        (void)advance();
        if (!expect(TokenKind::RBracket, "expected ']' in array type")) break;
        auto arr = std::make_unique<ArrayType>(start.span);
        arr->element = std::move(base);
        arr->span = spanFrom(start);
        base = std::move(arr);
    }
    return base;
}

// Parenthesized type or function type: `(T)` vs `(a: T) => R`.
TypePtr ParserImpl::parseParenOrFunctionType() {
    const Token start = peek();
    {
        Trial trial(*this);
        (void)advance();  // (
        ParamList params;
        bool ok = parseParamsBody(params);
        if (ok) {
            TypePtr ret;
            if (match(TokenKind::Colon)) ret = parseType();
            if (check(TokenKind::Arrow)) {
                trial.commit();
                (void)advance();
                auto fn = std::make_unique<FunctionType>(start.span);
                fn->params = std::move(params);
                // In type syntax the return type follows `=>` (e.g.
                // `(a: number) => void`); a pre-arrow `: T` is expression
                // arrow-function syntax and is handled by parseParenOrArrow.
                fn->returnType = ret ? std::move(ret) : parseType();
                fn->span = spanFrom(start);
                return fn;
            }
        }
    }
    TypePtr inner = [&]() -> TypePtr {
        (void)expect(TokenKind::LParen, "expected '('");
        TypePtr t = parseType();
        (void)expect(TokenKind::RParen, "expected ')'");
        return t;
    }();
    auto paren = std::make_unique<ParenthesizedType>(start.span);
    paren->inner = std::move(inner);
    paren->span = spanFrom(start);
    return paren;
}

TypePtr ParserImpl::parseTupleType() {
    const Token start = peek();
    (void)advance();
    auto tuple = std::make_unique<TupleType>(start.span);
    while (!check(TokenKind::RBracket) && !atEnd()) {
        TypePtr el = parseType();
        if (!el) break;
        tuple->elements.push_back(std::move(el));
        if (!match(TokenKind::Comma)) break;
    }
    (void)expect(TokenKind::RBracket, "expected ']' to close tuple type");
    tuple->span = spanFrom(start);
    return tuple;
}

TypePtr ParserImpl::parsePrimaryType() {
    if (check(TokenKind::LParen)) return parseParenOrFunctionType();
    if (check(TokenKind::LBracket)) return parseTupleType();
    if (check(TokenKind::LBrace)) return parseObjectType();

    const Token start = peek();

    // Literal types.
    if (check(TokenKind::StringLiteral)) {
        Token t = advance();
        auto lit = std::make_unique<LiteralType>(t.span);
        lit->literalKind = LiteralTypeKind::String;
        lit->stringValue = t.stringValue;
        return lit;
    }
    if (check(TokenKind::NumericLiteral)) {
        Token t = advance();
        auto lit = std::make_unique<LiteralType>(t.span);
        lit->literalKind = LiteralTypeKind::Number;
        lit->numberValue = t.numberValue;
        return lit;
    }
    if (check(TokenKind::KwTrue) || check(TokenKind::KwFalse)) {
        Token t = advance();
        auto lit = std::make_unique<LiteralType>(t.span);
        lit->literalKind = LiteralTypeKind::Bool;
        lit->boolValue = (t.kind == TokenKind::KwTrue);
        return lit;
    }

    // Prefixed escape-hatch type: r_'u32', c_'long long', ts_'number'.
    if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::StringLiteral) {
        std::string id = textOf(peek());
        if (id == "r_" || id == "c_" || id == "ts_") {
            (void)advance();
            Token str = advance();
            auto pref = std::make_unique<PrefixedType>(start.span);
            pref->prefix = id.substr(0, id.size() - 1);
            pref->name = str.stringValue;
            pref->span = spanFrom(start);
            return pref;
        }
    }

    // Named type (user type, built-in scalar, type parameter, `void`, ...).
    if (isIdentLike(peek()) || isTypeNameKeyword(peek().kind)) {
        Token nameTok = advance();
        auto named = std::make_unique<NamedType>(nameTok.span);
        named->name = textOf(nameTok);
        if (check(TokenKind::Less)) named->typeArgs = parseTypeArgs();
        named->span = spanFrom(start);
        return named;
    }

    report(diagcode::kUnexpectedToken, "expected a type");
    return nullptr;
}

TypeList ParserImpl::parseTypeArgs() {
    TypeList out;
    if (!expect(TokenKind::Less, "expected '<'")) return out;
    if (match(TokenKind::Greater)) return out;
    do {
        TypePtr t = parseType();
        if (!t) break;
        out.push_back(std::move(t));
        if (!match(TokenKind::Comma)) break;
    } while (!check(TokenKind::Greater) && !atEnd());
    (void)expectGreater();
    return out;
}

TypePtr ParserImpl::parseObjectType() {
    const Token start = peek();
    (void)expect(TokenKind::LBrace, "expected '{'");
    auto obj = std::make_unique<ObjectType>(start.span);
    while (!check(TokenKind::RBrace) && !atEnd()) {
        std::size_t before = ts_.cursor();
        SignatureMemberPtr member = parseSignatureMember();
        if (member) obj->members.push_back(std::move(member));
        if (!match(TokenKind::Comma) && !match(TokenKind::Semicolon) &&
            !check(TokenKind::RBrace)) {
            report(diagcode::kUnexpectedToken, "expected ',' or ';' in object type");
        }
        if (ts_.cursor() == before && !check(TokenKind::RBrace) && !atEnd()) {
            (void)advance();
        }
    }
    (void)expect(TokenKind::RBrace, "expected '}' to close object type");
    obj->span = spanFrom(start);
    return obj;
}

}  // namespace cp3
