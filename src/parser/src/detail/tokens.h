// =============================================================================
// src/parser/src/detail/tokens.h - PRIVATE parser state-machine header.
//
// Declares `ParserImpl` (all parser state) together with the token-cursor and
// error-recovery helpers shared by the parser's translation units:
//
//   parser.cpp        entry/orchestration, statement dispatch, imports/exports
//   statements.cpp    control-flow and simple statements
//   declarations.cpp  declarations, parameters, class/interface members
//   patterns.cpp      binding patterns (destructuring)
//   types.cpp         type syntax
//   webserverions.cpp   operator-precedence webserverion chain
//   primary.cpp       primary/postfix webserverions, call/member, literals
//   node_ids.cpp      nodeId assignment (DECISIONS.md D6)
//   tokens.cpp        cursor / diagnostic / recovery helpers  (this header)
//
// The frozen public boundary is `cp3/parser.hpp` (class `Parser`); everything
// here is implementation detail and is never included by another module
// (STYLE.md §3).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cp3/core/ast.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/parser.hpp"
#include "cp3/core/token.hpp"

namespace cp3 {

// Token kinds are used unqualified throughout the parser (`LParen`, `KwLet`,
// ...). `Identifier`, `StringLiteral`, and `RegExpLiteral` also name AST node
// types, so those are aliased before the enumeration is brought into scope.
using AstIdentifier = cp3::Identifier;
using AstStringLiteral = cp3::StringLiteral;
using AstRegExpLiteral = cp3::RegExpLiteral;
using enum TokenKind;

// --- free helpers shared by several parser units -----------------------------
// True when `k` can begin a statement (used by panic-mode synchronisation).
bool isStatementStart(TokenKind k);
// Maps an assignment token to its AssignOp, or false if not an assignment.
bool assignOpKind(TokenKind k, AssignOp& out);
// True when `e` may stand on the left of an assignment.
bool isAssignableTarget(const Expr& e);
// True for keywords accepted as a built-in scalar type name (void/unknown/...).
bool isTypeNameKeyword(TokenKind k);

// Modifiers (and the start token) collected before a class member's key.
struct MemberModifiers {
    Token start;
    AccessModifier access = AccessModifier::None;
    bool isStatic = false;
    bool isAbstract = false;
    bool isWeak = false;
    bool isReadonly = false;
    bool isAsync = false;
    bool isOverride = false;
    std::vector<ExprPtr> decorators;
};

// -----------------------------------------------------------------------------
// ParserImpl - all parser state lives here so the frozen `Parser` class shape
// (which only owns the TokenStream and the sink) stays untouched.
// -----------------------------------------------------------------------------
class ParserImpl {
public:
    ParserImpl(TokenStream& tokens, DiagnosticSink& sink) noexcept
        : ts_(tokens), sink_(&sink) {}

    ProgramPtr run();

private:
    // --- token cursor --------------------------------------------------------
    const Token& peek(std::size_t n = 0) const noexcept { return ts_.peek(n); }
    const Token& prev() const noexcept { return ts_.previous(); }
    bool check(TokenKind k) const noexcept { return ts_.peek().kind == k; }
    bool atEnd() const noexcept { return ts_.atEnd(); }
    Token advance() { return ts_.advance(); }
    bool match(TokenKind k) {
        if (check(k)) { (void)advance(); return true; }
        return false;
    }

    static bool isIdentLike(const Token& t) noexcept { return t.isIdentifierLike(); }
    static bool isNameToken(const Token& t) noexcept {
        return t.isIdentifierLike() || t.isKeywordToken();
    }
    static std::string textOf(const Token& t) {
        if (!t.lexeme.empty()) return std::string(t.lexeme);
        return std::string(tokenKindName(t.kind));
    }
    std::string advanceText() { return textOf(advance()); }

    SourceSpan spanFrom(const Token& start) const {
        return SourceSpan::merge(start.span, prev().span);
    }

    // --- diagnostics / recovery (tokens.cpp) --------------------------------
    void report(const std::string& code, const std::string& message);
    bool expect(TokenKind k, std::string what);
    std::string expectName(std::string what);
    // `>` handling for nested generics: `Box<Box<number>>` lexes `>>` as one
    // ShiftRight token, so a logically pending `>` is tracked.
    bool expectGreater();

    void synchronize();
    void consumeSemicolon();

    // --- module / statements (parser.cpp, statements.cpp) --------------------
    StmtPtr parseStatement();
    StmtPtr parseDeclaration(bool isExported);
    StmtPtr parseBlock();
    StmtPtr parseImportDeclaration();
    StmtPtr parseExportDeclaration();
    // Reads `{ a as b, ... }` into `decl->specifiers` (shared by imports).
    void parseImportSpecifiers(ImportDeclaration& decl);

    StmtPtr parseIfStatement();
    StmtPtr parseWhileStatement();
    StmtPtr parseDoWhileStatement();
    StmtPtr parseForStatement();
    StmtPtr parseClassicFor(const Token& start, NodePtr init);
    StmtPtr parseSwitchStatement();
    StmtPtr parseBreakStatement();
    StmtPtr parseContinueStatement();
    StmtPtr parseReturnStatement();
    StmtPtr parseThrowStatement();
    StmtPtr parseTryStatement();
    StmtPtr parseLabeledStatement();
    StmtPtr parsewebserverionStatement();
    // `@Name` / `@Name(args)` compile-time decorators. The webserverion is kept
    // in the AST; sema resolves the name to a `comptime function`.
    void parseDecorators(std::vector<ExprPtr>& out);
    StmtPtr parseDecoratedStatement();

    // --- declarations / members (declarations.cpp) ---------------------------
    StmtPtr parseVariableDeclaration(bool isStatement);
    VariableDeclaratorPtr parseVariableDeclarator();
    StmtPtr parseFunctionDeclaration(bool isExported);
    StmtPtr parseClassDeclaration(bool isExported);
    StmtPtr parseInterfaceDeclaration(bool isExported);
    StmtPtr parseEnumDeclaration(bool isExported);
    StmtPtr parseTypeAlias(bool isExported);

    ClassMemberPtr parseClassMember();
    void parseMemberModifiers(MemberModifiers& mods);
    ClassMemberPtr parseMethodMember(MemberModifiers& mods,
                                     ExprPtr key = nullptr,
                                     bool computed = false);
    ClassMemberPtr parseAccessorMember(MemberModifiers& mods);
    ClassMemberPtr parseConstructorMember(MemberModifiers& mods);
    ClassMemberPtr parsePropertyMember(MemberModifiers& mods,
                                       ExprPtr key = nullptr,
                                       bool computed = false);
    ClassMemberPtr parseStaticBlock(MemberModifiers& mods);
    SignatureMemberPtr parseSignatureMember();
    bool looksLikeAccessor() const;
    bool looksLikeConstructor() const;
    bool looksLikeReadonlyModifier() const;
    bool looksLikeOverrideModifier() const;
    ExprPtr parseMemberKey(bool& computed);
    ExprPtr parsePropertyKey();

    ParameterPtr parseParameter();
    bool parseParamsBody(ParamList& out);
    ParamList parseParamList();
    TypeParamList parseTypeParamsIfPresent();

    // --- binding patterns (patterns.cpp) ------------------------------------
    NodePtr parseBindingTarget();
    NodePtr parseArrayPattern();
    NodePtr parseObjectPattern();
    NodePtr parsePatternElement();
    ExprPtr makeIdentifier(const Token& t);

    // --- types (types.cpp) ---------------------------------------------------
    TypePtr parseType();
    TypePtr parseUnionType();
    TypePtr parsePostfixType();
    TypePtr parsePrimaryType();
    TypePtr parseParenOrFunctionType();
    TypePtr parseTupleType();
    TypePtr parseObjectType();
    TypeList parseTypeArgs();

    // --- webserverions (webserverions.cpp) --------------------------------------
    ExprPtr parsewebserverion();
    ExprPtr parseAssignment();
    ExprPtr parseConditional();
    ExprPtr parseNullish();
    ExprPtr parseLogicalOr();
    ExprPtr parseLogicalAnd();
    ExprPtr parseBitOr();
    ExprPtr parseBitXor();
    ExprPtr parseBitAnd();
    ExprPtr parseEquality();
    ExprPtr parseRelational();
    ExprPtr parseRange();
    ExprPtr parseShift();
    ExprPtr parseAdditive();
    ExprPtr parseMultiplicative();
    ExprPtr parsePower();
    ExprPtr parseUnary();
    ExprPtr parsePostfix();

    // --- primary/postfix webserverions (primary.cpp) --------------------------
    ExprPtr parseCallMember();
    ExprPtr parseMemberAccess(ExprPtr expr, const Token& start);
    ExprPtr parsePrimaryLiteral(const Token& t);
    ExprPtr parsePrimary();
    ExprPtr parseAsyncPrimary(const Token& t);
    ExprPtr parseNew();
    ExprPtr parseNewCallee();
    ExprPtr parseArrayLiteral();
    ExprPtr parseObjectLiteral();
    ExprPtr parseTemplateLiteral();
    ExprPtr parseParenOrArrow();
    ExprPtr parseGroupedwebserverion();
    ExprPtr parseSingleParamArrow(bool isAsync);
    ExprPtr parseGenericArrow();
    bool tryParseCallTypeArgs(TypeList& out);
    ExprList parseArguments();

    // --- nodeId assignment (node_ids.cpp, D6) -------------------------------
    void assignIds(Node& node);
    void assignExprChildren(Node& node);
    void assignOperatorChildren(Node& node);
    void assignPatternChildren(Node& node);
    void assignStmtChildren(Node& node);
    void assignDeclChildren(Node& node);
    void assignControlChildren(Node& node);
    void assignMemberChildren(Node& node);
    void assignTypeChildren(Node& node);

    template <typename T>
    void assignAll(const std::vector<std::unique_ptr<T>>& list) {
        for (const auto& p : list) {
            if (p) assignIds(*p);
        }
    }
    void assignOpt(const Node* node) {
        if (node) assignIds(const_cast<Node&>(*node));
    }

    // CallExpr and NewExpr share the same child shape (callee, typeArgs, args).
    template <typename T>
    void assignCallChildren(T& e) {
        assignOpt(e.callee.get());
        assignAll(e.typeArgs);
        for (const auto& a : e.arguments) {
            if (a) assignIds(*a);
        }
    }

    // ForOfStatement and ForInStatement share the same child shape.
    template <typename T>
    void assignForLoopChildren(T& s) {
        assignOpt(s.left.get());
        assignOpt(s.right.get());
        assignOpt(s.body.get());
    }

    // --- state ---------------------------------------------------------------
    TokenStream& ts_;
    DiagnosticSink* sink_;
    const Token* anchor_ = nullptr;
    std::uint32_t pendingGT_ = 0;
    bool noIn_ = false;
    std::uint32_t nextId_ = 1;

    struct AnchorScope {
        ParserImpl& p;
        const Token* saved;
        AnchorScope(ParserImpl& impl, const Token& t) : p(impl), saved(impl.anchor_) {
            p.anchor_ = &t;
        }
        ~AnchorScope() { p.anchor_ = saved; }
    };

    // Trial parse: buffers diagnostics and restores the cursor on failure.
    // A successful trial forwards its buffered diagnostics to the parent sink
    // immediately. This matters when the accepted construct has a body: errors
    // in that body must not remain hidden until the trial object is destroyed.
    struct Trial {
        ParserImpl& p;
        std::size_t cursor;
        DiagnosticSink* sink;
        std::uint32_t pendingGT;
        CollectingDiagnosticSink diagnostics;
        bool committed = false;
        explicit Trial(ParserImpl& impl)
            : p(impl),
              cursor(impl.ts_.cursor()),
              sink(impl.sink_),
              pendingGT(impl.pendingGT_) {
            p.sink_ = &diagnostics;
        }
        void commit() {
            if (committed) return;
            committed = true;
            p.sink_ = sink;
            for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
                sink->report(diagnostic);
            }
        }
        ~Trial() {
            p.sink_ = sink;
            if (!committed) {
                p.ts_.setCursor(cursor);
                p.pendingGT_ = pendingGT;
            }
        }
    };
};

}  // namespace cp3
