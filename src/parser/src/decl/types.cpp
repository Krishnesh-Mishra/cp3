// =============================================================================
// src/parser/src/types.cpp - type syntax.
//
// Implements the type grammar: union types, postfix array/indexed-access
// types, and primary types (parenthesized / function types, tuples, object
// type literals, literal types, prefixed escape-hatch types `r_'u32'`, named/
// generic types, and the v0.2.6 type operators `typeof <entity>` and
// `keyof <type>`).
// =============================================================================
#include "detail/tokens.h"

#include <memory>
#include <string>
#include <utility>

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/host/native_types.hpp"

namespace cp3 {

TypePtr ParserImpl::parseType() {
    // `readonly T[]` / `readonly [A, B]`: a read-through view of an array or
    // tuple. `readonly` is a contextual marker (a regular-class member modifier
    // too); TypeScript's mutability rule is compile-time only and cp3 has no
    // const-tracking, so the qualifier is accepted and dropped. Applying it to
    // a non-array is a type error.
    if (check(TokenKind::Identifier) && textOf(peek()) == "readonly") {
        const Token start = advance();
        const Token qualifier = start;
        TypePtr t = parsePostfixType();
        if (!t) return nullptr;
        if (t->kind != NodeKind::ArrayType && t->kind != NodeKind::TupleType) {
            report(diagcode::kTypeMismatch,
                   "readonly may only qualify an array or tuple type");
        }
        t->span = spanFrom(qualifier);
        return t;
    }
    return parseUnionType();
}

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
    // Postfix array types (`T[]`) and indexed-access types (`T[K]`). The
    // two disambiguate on the token right after `[`: `]` means array, any
    // type means indexed access (v0.2.6).
    for (;;) {
        if (!check(TokenKind::LBracket)) break;
        (void)advance();
        if (match(TokenKind::RBracket)) {
            auto arr = std::make_unique<ArrayType>(start.span);
            arr->element = std::move(base);
            arr->span = spanFrom(start);
            base = std::move(arr);
            continue;
        }
        TypePtr index = parseType();
        if (!index) return nullptr;
        (void)expect(TokenKind::RBracket, "expected ']' in indexed access type");
        auto idx = std::make_unique<IndexedAccessType>(start.span);
        idx->object = std::move(base);
        idx->index = std::move(index);
        idx->span = spanFrom(start);
        base = std::move(idx);
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

// `typeof <entity>` in type position (v0.2.6): the operand is a value name
// (`typeof a`) or a member chain on one (`typeof a.b.c`), built as an
// ordinary expression so sema re-infers it. Arbitrary expressions are not
// accepted (TypeScript parity).
ExprPtr ParserImpl::parseEntityName() {
    if (!isIdentLike(peek())) {
        report(diagcode::kUnexpectedToken, "expected a name after 'typeof' in a type");
        return nullptr;
    }
    ExprPtr expr = makeIdentifier(advance());
    while (check(TokenKind::Dot)) {
        (void)advance();
        const Token propTok = peek();
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
    }
    return expr;
}

TypePtr ParserImpl::parseTypeofType() {
    const Token start = advance();  // typeof
    ExprPtr operand = parseEntityName();
    if (!operand) return nullptr;
    auto tt = std::make_unique<TypeofType>(start.span);
    tt->operand = std::move(operand);
    tt->span = spanFrom(start);
    return tt;
}

// `keyof T` in type position (v0.2.6). `keyof` is a contextual marker: a
// regular type name `keyof` does not exist, so an identifier spelling
// "keyof" followed by a type starts the key union (cf. `readonly`).
TypePtr ParserImpl::parseKeyofType() {
    const Token start = advance();  // keyof
    TypePtr operand = parsePostfixType();
    if (!operand) return nullptr;
    auto kt = std::make_unique<KeyofType>(start.span);
    kt->operand = std::move(operand);
    kt->span = spanFrom(start);
    return kt;
}

TypePtr ParserImpl::parsePrimaryType() {
    if (check(TokenKind::LParen)) return parseParenOrFunctionType();
    if (check(TokenKind::LBracket)) return parseTupleType();
    if (check(TokenKind::LBrace)) return parseObjectType();
    if (check(TokenKind::KwTypeof)) return parseTypeofType();
    if (check(TokenKind::Identifier) && textOf(peek()) == "keyof") {
        return parseKeyofType();
    }

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
        lit->numberRaw = std::string(textOf(t));
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
        // A qualified C++ name in type position (`std::string`,
        // `std::size_t`): the lexer emits `::` as two colons. Fold the
        // qualified spelling so the project's native table can resolve it.
        while (check(TokenKind::Colon) && peek(1).kind == TokenKind::Colon &&
               isIdentLike(peek(2))) {
            (void)advance();  // ':'
            (void)advance();  // ':'
            named->name += "::";
            named->name += textOf(advance());
        }
        // A project typeSystem of "cpp" spells plain names the C++ way:
        // `let a: long long = 5;`. Keep the supported multi-word spellings
        // (`long long int`, `unsigned short`, `signed char`, ...) in one
        // NamedType; any other identifier sequence is left to the grammar.
        while (isIdentLike(peek()) &&
               isCppTypeContinuation(named->name, textOf(peek()))) {
            named->name += " ";
            named->name += textOf(advance());
        }
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
