// =============================================================================
// src/parser/src/literals.cpp - array, object and template literals.
//
// Parses `[...]` (including holes and spreads), `{ ... }` (computed keys,
// shorthand values per DECISIONS.md D13, spreads, and the `{ weak: value }`
// wrapper) and template literals (the flat TemplateHead / interpolation /
// TemplateMiddle... / TemplateTail sequence, DECISIONS.md D7).
// =============================================================================
#include "detail/tokens.h"

#include <memory>
#include <utility>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

ExprPtr ParserImpl::parseArrayLiteral() {
    const Token start = peek();
    (void)advance();  // [
    auto arr = std::make_unique<ArrayLiteral>(start.span);
    while (!check(TokenKind::RBracket) && !atEnd()) {
        if (check(TokenKind::Comma)) {
            arr->elements.push_back(nullptr);  // hole
            (void)advance();
            continue;
        }
        if (match(TokenKind::Ellipsis)) {
            Token spreadStart = prev();
            auto spread = std::make_unique<SpreadElement>(spreadStart.span);
            spread->argument = parseAssignment();
            spread->span = spanFrom(spreadStart);
            arr->elements.push_back(std::move(spread));
        } else {
            ExprPtr e = parseAssignment();
            if (!e) break;
            arr->elements.push_back(std::move(e));
        }
        if (!match(TokenKind::Comma)) break;
    }
    (void)expect(TokenKind::RBracket, "expected ']' to close array literal");
    arr->span = spanFrom(start);
    return arr;
}

ExprPtr ParserImpl::parseObjectLiteral() {
    const Token start = peek();
    (void)advance();  // {
    auto obj = std::make_unique<ObjectLiteral>(start.span);
    while (!check(TokenKind::RBrace) && !atEnd()) {
        Token propStart = peek();
        if (match(TokenKind::Ellipsis)) {
            // No dedicated object-spread node: an ObjectProperty whose value is
            // the SpreadElement and whose key is null (documented composition).
            auto prop = std::make_unique<ObjectProperty>(propStart.span);
            auto spread = std::make_unique<SpreadElement>(propStart.span);
            spread->argument = parseAssignment();
            spread->span = spanFrom(propStart);
            prop->value = std::move(spread);
            prop->span = spanFrom(propStart);
            obj->properties.push_back(std::move(prop));
            if (!match(TokenKind::Comma)) break;
            continue;
        }

        auto prop = std::make_unique<ObjectProperty>(propStart.span);
        if (match(TokenKind::LBracket)) {
            prop->computed = true;
            prop->key = parseAssignment();
            (void)expect(TokenKind::RBracket, "expected ']' after computed key");
        } else {
            prop->key = parsePropertyKey();
            if (!prop->key) {
                report(diagcode::kUnexpectedToken, "expected a property name");
                if (!check(TokenKind::RBrace)) (void)advance();
                continue;
            }
        }

        if (match(TokenKind::Colon)) {
            prop->value = parseAssignment();
        } else {
            // Shorthand: `{ a }` -> value = Identifier("a") (DECISIONS.md D13).
            prop->shorthand = true;
            if (prop->key && prop->key->kind == NodeKind::Identifier) {
                const auto& ident = static_cast<const AstIdentifier&>(*prop->key);
                prop->value = std::make_unique<AstIdentifier>(ident.span, ident.name);
            } else {
                report(diagcode::kUnexpectedToken, "invalid shorthand property");
            }
        }
        prop->span = spanFrom(propStart);
        obj->properties.push_back(std::move(prop));
        if (!match(TokenKind::Comma)) break;
    }
    (void)expect(TokenKind::RBrace, "expected '}' to close object literal");
    obj->span = spanFrom(start);

    // `{ weak: value }` is the object-literal weak-link wrapper (spec §11).
    if (obj->properties.size() == 1) {
        const ObjectProperty& only = *obj->properties.front();
        if (!only.computed && !only.shorthand && only.key &&
            only.key->kind == NodeKind::Identifier) {
            const auto& keyIdent = static_cast<const AstIdentifier&>(*only.key);
            if (keyIdent.name == "weak" &&
                (!only.value || only.value->kind != NodeKind::SpreadElement)) {
                auto weak = std::make_unique<WeakExpr>(obj->span);
                weak->value = std::move(obj->properties.front()->value);
                return weak;
            }
        }
    }
    return obj;
}

ExprPtr ParserImpl::parseTemplateLiteral() {
    const Token start = peek();
    (void)advance();  // TemplateHead
    auto tl = std::make_unique<TemplateLiteral>(start.span);
    {
        auto quasi = std::make_unique<TemplateElement>(start.span);
        quasi->raw = start.rawText;
        quasi->cooked = start.stringValue;
        quasi->tail = false;
        tl->quasis.push_back(std::move(quasi));
    }
    for (;;) {
        ExprPtr e = parseExpression();
        if (e) tl->expressions.push_back(std::move(e));

        if (check(TokenKind::TemplateMiddle)) {
            Token mid = advance();
            auto quasi = std::make_unique<TemplateElement>(mid.span);
            quasi->raw = mid.rawText;
            quasi->cooked = mid.stringValue;
            quasi->tail = false;
            tl->quasis.push_back(std::move(quasi));
            continue;
        }
        if (check(TokenKind::TemplateTail)) {
            Token tail = advance();
            auto quasi = std::make_unique<TemplateElement>(tail.span);
            quasi->raw = tail.rawText;
            quasi->cooked = tail.stringValue;
            quasi->tail = true;
            tl->quasis.push_back(std::move(quasi));
            break;
        }
        report(diagcode::kUnexpectedToken, "expected a template continuation");
        break;
    }
    tl->span = spanFrom(start);
    return tl;
}
}  // namespace cp3
