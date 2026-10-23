// =============================================================================
// src/parser/src/declarations.cpp - declarations, members, and parameters.
//
// Implements variable/function/class/interface/enum/type-alias declarations,
// class & interface members (methods, accessors, constructors, properties),
// and the parameter / type-parameter lists shared by functions, methods,
// arrow functions and function types.
// =============================================================================
#include "detail/tokens.h"

#include <memory>
#include <string>
#include <utility>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

// =============================================================================
// Declarations
// =============================================================================
StmtPtr ParserImpl::parseVariableDeclaration(bool isStatement) {
    const Token start = peek();
    VarKind kind = VarKind::Let;
    switch (peek().kind) {
        case TokenKind::KwVar: kind = VarKind::Var; break;
        case TokenKind::KwConst: kind = VarKind::Const; break;
        default: kind = VarKind::Let; break;
    }
    (void)advance();

    auto decl = std::make_unique<VariableDeclaration>(start.span);
    decl->kind = kind;
    do {
        VariableDeclaratorPtr d = parseVariableDeclarator();
        if (!d) break;
        decl->declarations.push_back(std::move(d));
    } while (match(TokenKind::Comma));

    if (isStatement) consumeSemicolon();
    decl->span = spanFrom(start);
    return decl;
}

VariableDeclaratorPtr ParserImpl::parseVariableDeclarator() {
    const Token start = peek();
    auto decl = std::make_unique<VariableDeclarator>(start.span);
    decl->id = parseBindingTarget();
    if (match(TokenKind::Colon)) decl->type = parseType();
    if (match(TokenKind::Assign)) decl->init = parseAssignment();
    decl->span = spanFrom(start);
    return decl;
}

StmtPtr ParserImpl::parseFunctionDeclaration(bool isExported) {
    const Token start = peek();
    bool isAsync = false;
    if (check(TokenKind::KwAsync)) {
        isAsync = true;
        (void)advance();
    }
    (void)expect(TokenKind::KwFunction, "expected 'function'");
    if (check(TokenKind::Star)) {
        report(diagcode::kUnexpectedToken, "generators are not supported");
        (void)advance();
    }

    auto fn = std::make_unique<FunctionDeclaration>(start.span);
    fn->isAsync = isAsync;
    fn->isExported = isExported;
    fn->name = expectName("a function name");
    fn->typeParams = parseTypeParamsIfPresent();
    fn->params = parseParamList();
    if (match(TokenKind::Colon)) fn->returnType = parseType();
    if (check(TokenKind::LBrace)) {
        fn->body = parseBlock();
    } else {
        consumeSemicolon();
    }
    fn->span = spanFrom(start);
    return fn;
}

StmtPtr ParserImpl::parseClassDeclaration(bool isExported) {
    const Token start = peek();
    bool isAbstract = false;
    if (match(TokenKind::KwAbstract)) isAbstract = true;
    (void)expect(TokenKind::KwClass, "expected 'class'");

    auto cls = std::make_unique<ClassDeclaration>(start.span);
    cls->isAbstract = isAbstract;
    cls->isExported = isExported;
    cls->name = expectName("a class name");
    cls->typeParams = parseTypeParamsIfPresent();
    if (match(TokenKind::KwExtends)) {
        cls->extendsType = parseType();
        // `extends A, B` (multiple inheritance) is not part of the grammar;
        // recovery keeps the class body parseable and reports once.
        if (check(TokenKind::Comma)) {
            report(diagcode::kUnexpectedToken, "multiple inheritance is not supported");
            while (match(TokenKind::Comma)) (void)parseType();
        }
    }
    if (match(TokenKind::KwImplements)) {
        do {
            TypePtr t = parseType();
            if (!t) break;
            cls->implementsTypes.push_back(std::move(t));
        } while (match(TokenKind::Comma));
    }

    (void)expect(TokenKind::LBrace, "expected '{' to open class body");
    while (!check(TokenKind::RBrace) && !atEnd()) {
        std::size_t before = ts_.cursor();
        ClassMemberPtr member = parseClassMember();
        if (member) cls->members.push_back(std::move(member));
        if (ts_.cursor() == before && !check(TokenKind::RBrace) && !atEnd()) {
            (void)advance();
        }
    }
    (void)expect(TokenKind::RBrace, "expected '}' to close class body");
    cls->span = spanFrom(start);
    return cls;
}

StmtPtr ParserImpl::parseInterfaceDeclaration(bool isExported) {
    const Token start = peek();
    (void)expect(TokenKind::KwInterface, "expected 'interface'");
    auto iface = std::make_unique<InterfaceDeclaration>(start.span);
    iface->isExported = isExported;
    iface->name = expectName("an interface name");
    iface->typeParams = parseTypeParamsIfPresent();
    if (match(TokenKind::KwExtends)) {
        do {
            TypePtr t = parseType();
            if (!t) break;
            iface->extendsTypes.push_back(std::move(t));
        } while (match(TokenKind::Comma));
    }
    (void)expect(TokenKind::LBrace, "expected '{' to open interface body");
    while (!check(TokenKind::RBrace) && !atEnd()) {
        std::size_t before = ts_.cursor();
        SignatureMemberPtr member = parseSignatureMember();
        if (member) iface->members.push_back(std::move(member));
        if (!match(TokenKind::Comma) && !match(TokenKind::Semicolon) &&
            !check(TokenKind::RBrace)) {
            report(diagcode::kUnexpectedToken, "expected ',' or ';'");
        }
        if (ts_.cursor() == before && !check(TokenKind::RBrace) && !atEnd()) {
            (void)advance();
        }
    }
    (void)expect(TokenKind::RBrace, "expected '}' to close interface body");
    iface->span = spanFrom(start);
    return iface;
}

StmtPtr ParserImpl::parseEnumDeclaration(bool isExported) {
    const Token start = peek();
    (void)expect(TokenKind::KwEnum, "expected 'enum'");
    auto en = std::make_unique<EnumDeclaration>(start.span);
    en->isExported = isExported;
    en->name = expectName("an enum name");
    (void)expect(TokenKind::LBrace, "expected '{' to open enum body");
    while (!check(TokenKind::RBrace) && !atEnd()) {
        if (!isNameToken(peek())) {
            report(diagcode::kUnexpectedToken, "expected an enum member name");
            if (!check(TokenKind::RBrace)) (void)advance();
            continue;
        }
        Token nameTok = advance();
        auto member = std::make_unique<EnumMember>(nameTok.span);
        member->name = textOf(nameTok);
        if (match(TokenKind::Assign)) {
            member->initializer = parseAssignment();
            if (member->initializer && member->initializer->kind == NodeKind::StringLiteral) {
                en->stringBacked = true;
            }
        }
        member->span = nameTok.span;
        en->members.push_back(std::move(member));
        if (!match(TokenKind::Comma)) break;
    }
    (void)expect(TokenKind::RBrace, "expected '}' to close enum body");
    en->span = spanFrom(start);
    return en;
}

StmtPtr ParserImpl::parseTypeAlias(bool isExported) {
    const Token start = peek();
    (void)expect(TokenKind::KwType, "expected 'type'");
    auto alias = std::make_unique<TypeAliasDeclaration>(start.span);
    alias->isExported = isExported;
    alias->name = expectName("a type alias name");
    alias->typeParams = parseTypeParamsIfPresent();
    if (expect(TokenKind::Assign, "expected '=' in type alias")) {
        alias->type = parseType();
    }
    consumeSemicolon();
    alias->span = spanFrom(start);
    return alias;
}

// =============================================================================
// Parameters / type parameters
// =============================================================================
ParameterPtr ParserImpl::parseParameter() {
    const Token start = peek();
    auto param = std::make_unique<Parameter>(start.span);

    if (match(TokenKind::KwRef)) param->isRef = true;

    if (match(TokenKind::Ellipsis)) {
        param->isRest = true;
        Token restStart = prev();
        auto rest = std::make_unique<RestElement>(restStart.span);
        rest->argument = parseBindingTarget();
        rest->span = spanFrom(restStart);
        param->name = std::move(rest);
    } else {
        param->name = parseBindingTarget();
        if (!param->name) return nullptr;
    }

    if (match(TokenKind::Question)) param->optional = true;
    if (match(TokenKind::Colon)) param->type = parseType();
    if (match(TokenKind::Assign)) param->defaultValue = parseAssignment();
    param->span = spanFrom(start);
    return param;
}

bool ParserImpl::parseParamsBody(ParamList& out) {
    while (!check(TokenKind::RParen) && !atEnd()) {
        ParameterPtr p = parseParameter();
        if (!p) return false;
        out.push_back(std::move(p));
        if (!match(TokenKind::Comma)) break;
    }
    if (!match(TokenKind::RParen)) return false;
    return true;
}

ParamList ParserImpl::parseParamList() {
    ParamList out;
    if (!expect(TokenKind::LParen, "expected '('")) return out;
    (void)parseParamsBody(out);
    return out;
}

TypeParamList ParserImpl::parseTypeParamsIfPresent() {
    TypeParamList out;
    if (!check(TokenKind::Less)) return out;
    (void)advance();
    if (match(TokenKind::Greater)) return out;

    do {
        Token tpStart = peek();
        auto tp = std::make_unique<TypeParameter>(tpStart.span);
        tp->name = expectName("a type parameter name");
        if (match(TokenKind::KwExtends)) tp->constraint = parseType();
        if (match(TokenKind::Assign)) tp->defaultType = parseType();
        tp->span = spanFrom(tpStart);
        out.push_back(std::move(tp));
        if (!match(TokenKind::Comma)) break;
    } while (!check(TokenKind::Greater) && !atEnd());

    (void)expectGreater();
    return out;
}

}  // namespace cp3
