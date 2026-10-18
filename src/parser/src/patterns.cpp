// =============================================================================
// src/parser/src/patterns.cpp - binding patterns (destructuring targets).
//
// Implements the binding targets accepted after `let`/`var`/`const`, in
// parameters, and in `catch (…)`: identifiers, array patterns, object patterns
// (including shorthand, computed keys and defaults) and rest elements.
// =============================================================================
#include "detail/tokens.h"

#include <memory>
#include <string>
#include <utility>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {

NodePtr ParserImpl::parseBindingTarget() {
    if (check(TokenKind::LBracket)) return parseArrayPattern();
    if (check(TokenKind::LBrace)) return parseObjectPattern();
    if (isIdentLike(peek())) {
        Token t = advance();
        return makeIdentifier(t);
    }
    report(diagcode::kUnexpectedToken, "expected a binding name");
    return nullptr;
}

ExprPtr ParserImpl::makeIdentifier(const Token& t) {
    return std::make_unique<AstIdentifier>(t.span, textOf(t));
}

NodePtr ParserImpl::parsePatternElement() {
    NodePtr target = parseBindingTarget();
    if (!target) return nullptr;
    if (match(TokenKind::Assign)) {
        auto ap = std::make_unique<AssignmentPattern>(target->span);
        ap->left = std::move(target);
        ap->right = parseAssignment();
        ap->span = spanFrom(prev());
        return ap;
    }
    return target;
}

NodePtr ParserImpl::parseArrayPattern() {
    const Token start = peek();
    (void)expect(TokenKind::LBracket, "expected '['");
    auto pat = std::make_unique<ArrayPattern>(start.span);
    while (!check(TokenKind::RBracket) && !atEnd()) {
        if (check(TokenKind::Comma)) {
            pat->elements.push_back(nullptr);
            (void)advance();
            continue;
        }
        if (match(TokenKind::Ellipsis)) {
            Token restStart = prev();
            auto rest = std::make_unique<RestElement>(restStart.span);
            rest->argument = parseBindingTarget();
            rest->span = spanFrom(restStart);
            pat->rest = std::move(rest);
            break;
        }
        NodePtr el = parsePatternElement();
        pat->elements.push_back(std::move(el));
        if (!match(TokenKind::Comma)) break;
    }
    (void)expect(TokenKind::RBracket, "expected ']' to close array pattern");
    pat->span = spanFrom(start);
    return pat;
}

NodePtr ParserImpl::parseObjectPattern() {
    const Token start = peek();
    (void)expect(TokenKind::LBrace, "expected '{'");
    auto pat = std::make_unique<ObjectPattern>(start.span);
    while (!check(TokenKind::RBrace) && !atEnd()) {
        if (match(TokenKind::Ellipsis)) {
            Token restStart = prev();
            auto rest = std::make_unique<RestElement>(restStart.span);
            rest->argument = parseBindingTarget();
            rest->span = spanFrom(restStart);
            pat->rest = std::move(rest);
            break;
        }
        Token propStart = peek();
        auto prop = std::make_unique<ObjectPatternProperty>(propStart.span);
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
            prop->value = parsePatternElement();
        } else {
            prop->shorthand = true;
            NodePtr value;
            if (prop->key && prop->key->kind == NodeKind::Identifier) {
                const auto& ident = static_cast<const AstIdentifier&>(*prop->key);
                value = std::make_unique<AstIdentifier>(ident.span, ident.name);
            } else {
                report(diagcode::kUnexpectedToken, "invalid shorthand property in pattern");
            }
            if (match(TokenKind::Assign)) {
                auto ap = std::make_unique<AssignmentPattern>(propStart.span);
                ap->left = std::move(value);
                ap->right = parseAssignment();
                ap->span = spanFrom(propStart);
                prop->value = std::move(ap);
            } else {
                prop->value = std::move(value);
            }
        }
        prop->span = spanFrom(propStart);
        pat->properties.push_back(std::move(prop));
        if (!match(TokenKind::Comma)) break;
    }
    (void)expect(TokenKind::RBrace, "expected '}' to close object pattern");
    pat->span = spanFrom(start);
    return pat;
}

}  // namespace cp3
