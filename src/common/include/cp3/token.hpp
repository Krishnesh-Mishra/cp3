// =============================================================================
// cp3/token.hpp — the complete TokenKind enumeration and Token type.
//
// FROZEN INTERFACE (Beta One). See ARCHITECTURE.md.
//
// THIS IS THE COMPLETE TOKEN SET FOR C+++ (Beta One). The lexer must never
// invent a TokenKind; the parser must never pattern-match on lexeme text when
// a TokenKind exists.
//
// Template literals (`` `a${b}c` ``) are emitted as a small sequence, exactly
// like JavaScript engines do, so the parser can drive re-lexing of the
// interpolated expressions:
//
//     TemplateHead   "`a${"          (template with >= 1 substitution, opening)
//     ... tokens of expression b ...
//     TemplateMiddle "}c${"          (zero or more; only if another sub follows)
//     TemplateTail   "}" + "`"       (closing part after the last expression)
//     TemplateTail   also covers "}`" for the single-substitution case
//   and for a template with NO substitution:
//     TemplateString "`a c`"         (NoSubstitutionTemplate)
//
// Comments are normally skipped, but the lexer may emit LineComment/BlockComment
// in "trivia" mode (cp3 fmt needs them).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cp3/source.hpp"

namespace cp3 {

enum class TokenKind {
    // -- sentinels ----------------------------------------------------------
    EndOfFile,
    Unknown,           // lexed something we cannot classify
    Invalid,           // a hard lexical error (already reported)

    // -- literals -----------------------------------------------------------
    NumericLiteral,    // 123, 3.14, 0x1F, 0b101, 1_000_000
    StringLiteral,     // "..." or '...'
    RegExpLiteral,     // /pattern/flags
    TemplateString,    // `no substitution` (NoSubstitutionTemplate)
    TemplateHead,      // `...${
    TemplateMiddle,    // }...${
    TemplateTail,      // }...`  (also covers the single-substitution close)

    // -- identifiers --------------------------------------------------------
    Identifier,
    PrivateIdentifier,  // #name  (reserved; not in Beta One surface)

    // -- trivia (formatter only) -------------------------------------------
    LineComment,        // // ...
    BlockComment,       // /* ... */

    // -- keywords -----------------------------------------------------------
    KwLet, KwVar, KwConst,
    KwFunction,
    KwReturn,
    KwIf, KwElse,
    KwWhile, KwDo, KwFor, KwOf, KwIn,
    KwSwitch, KwCase, KwDefault,
    KwBreak, KwContinue,
    KwThrow, KwTry, KwCatch, KwFinally,
    KwClass, KwInterface, KwExtends, KwImplements,
    KwAbstract, KwStatic, KwPublic, KwPrivate, KwProtected,
    KwEnum,
    KwNew, KwThis,
    KwImport, KwExport, KwFrom, KwAs, KwNative,
    KwAsync, KwAwait,
    KwWeak, KwRef,
    KwTypeof,
    KwGet, KwSet,
    KwTrue, KwFalse,
    KwNull, KwUndefined,
    KwVoid, KwUnknown,
    KwType,             // `type X = ...`

    // -- reserved keywords (parse, then a specific cp3-native error) --------
    KwAny,              // banned: there is no `any` (CP3806)
    KwDelete,           // banned: `delete obj.prop` (CP3801)
    KwYield,            // generators deferred (CP3807)

    // -- punctuation --------------------------------------------------------
    LParen, RParen,         // ( )
    LBrace, RBrace,         // { }
    LBracket, RBracket,     // [ ]
    Semicolon,              // ;
    Comma,                  // ,
    Dot,                    // .
    DotDot,                 // ..  (range operator: 0..numbers.length)
    Ellipsis,               // ...
    Arrow,                  // =>
    Colon,                  // :
    Question,               // ?
    QuestionDot,            // ?.
    QuestionQuestion,       // ??

    // -- operators ----------------------------------------------------------
    Plus, Minus, Star, Slash, Percent, StarStar,   // + - * / %  **
    PlusPlus, MinusMinus,                           // ++ --
    Assign,                                         // =
    PlusAssign, MinusAssign, StarAssign, SlashAssign, PercentAssign, StarStarAssign,
    AmpersandAssign, PipeAssign, CaretAssign, ShiftLeftAssign, ShiftRightAssign,
    UnsignedShiftRightAssign,                       // >>>=  (appended; DECISIONS D53)
    AndAndAssign, OrOrAssign, QuestionQuestionAssign,  // &&= ||= ??=
    Equal, NotEqual,                                // == !=
    StrictEqual, StrictNotEqual,                    // === !==
    Less, Greater, LessEqual, GreaterEqual,         // < > <= >=
    AndAnd, OrOr, Bang,                             // && || !
    Ampersand, Pipe, Caret, Tilde,                  // & | ^ ~
    ShiftLeft, ShiftRight, UnsignedShiftRight,      // << >> >>>

    // -- reserved punctuation ----------------------------------------------
    At,                 // @  (decorators deferred)
    Backslash,          // \  (invalid outside strings)
};

// Human-readable name, for diagnostics and tests (returns "Identifier", etc.).
const char* tokenKindName(TokenKind kind) noexcept;

// True for the keyword kinds (including reserved keywords). Useful for the
// parser when deciding whether an Identifier may follow.
bool isKeyword(TokenKind kind) noexcept;
// True for keywords that are *also* usable as contextual identifiers in the
// grammar (async, await, get, set, from, as, of, static, type, ...).
bool isContextualKeyword(TokenKind kind) noexcept;

// A single lexed token.
//
// Lifetime: `lexeme` and `stringValue` are views/copies related to the
// SourceFile the lexer was constructed with. `lexeme` is a view into
// SourceFile::text and is valid as long as that file is alive (its
// SourceManager outlives the token stream).
struct Token {
    TokenKind kind = TokenKind::Unknown;
    SourceSpan span{};
    std::string_view lexeme{};   // raw source text of the token

    // Decoded payloads (only the relevant one is set):
    double numberValue = 0.0;    // NumericLiteral
    std::string stringValue;     // StringLiteral / Template* (decoded, escapes applied)
    std::string rawText;         // Template*: the *raw* (uncooked) inner text

    bool precededByNewline = false;  // for ASI-style decisions
    bool unterminated = false;       // string/template/comment that hit EOF

    bool is(TokenKind k) const noexcept { return kind == k; }
    bool isKeywordToken() const noexcept { return isKeyword(kind); }
    bool isIdentifierLike() const noexcept {
        return kind == TokenKind::Identifier ||
               (isContextualKeyword(kind) && kind != TokenKind::KwTrue &&
                kind != TokenKind::KwFalse && kind != TokenKind::KwNull &&
                kind != TokenKind::KwUndefined);
    }
};

// A lightweight cursor over a token vector. The parser owns one of these.
class TokenStream {
public:
    TokenStream() = default;
    explicit TokenStream(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    bool atEnd() const noexcept { return cursor_ >= tokens_.size() ||
                                         peek().kind == TokenKind::EndOfFile; }

    const Token& peek(std::size_t lookahead = 0) const noexcept;
    Token advance();
    const Token& previous() const noexcept;

    // Template literals are lexed flat (DECISIONS.md D7): the lexer emits the
    // whole TemplateHead / interpolation tokens / TemplateMiddle… /
    // TemplateTail sequence itself, so TokenStream has no re-lex callback and
    // the parser never asks the lexer to rescan a template continuation.
    std::size_t cursor() const noexcept { return cursor_; }
    void setCursor(std::size_t c) noexcept { cursor_ = c; }
    std::size_t size() const noexcept { return tokens_.size(); }
    const std::vector<Token>& tokens() const noexcept { return tokens_; }

    static const Token& eofToken();

private:
    std::vector<Token> tokens_;
    std::size_t cursor_ = 0;
};

}  // namespace cp3
