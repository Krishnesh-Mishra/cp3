#include "cp3/core/token.hpp"

#include <cstddef>

namespace cp3 {

// Lookup table for tokenKindName(): one name per TokenKind, listed in
// declaration order (src/common/include/cp3/token.hpp). It is indexed by the
// enum's underlying value; any out-of-range value yields "Unknown", exactly
// as the previous switch's default did. The enum is a frozen interface, so
// keep this table in lockstep if TokenKind ever gains or renames a member.
constexpr const char* kTokenKindNames[] = {
    "EndOfFile",  // EndOfFile
    "Unknown",  // Unknown
    "Invalid",  // Invalid
    "NumericLiteral",  // NumericLiteral
    "StringLiteral",  // StringLiteral
    "RegExpLiteral",  // RegExpLiteral
    "TemplateString",  // TemplateString
    "TemplateHead",  // TemplateHead
    "TemplateMiddle",  // TemplateMiddle
    "TemplateTail",  // TemplateTail
    "Identifier",  // Identifier
    "PrivateIdentifier",  // PrivateIdentifier
    "LineComment",  // LineComment
    "BlockComment",  // BlockComment
    "let",  // KwLet
    "var",  // KwVar
    "const",  // KwConst
    "function",  // KwFunction
    "return",  // KwReturn
    "if",  // KwIf
    "else",  // KwElse
    "while",  // KwWhile
    "do",  // KwDo
    "for",  // KwFor
    "of",  // KwOf
    "in",  // KwIn
    "switch",  // KwSwitch
    "case",  // KwCase
    "default",  // KwDefault
    "break",  // KwBreak
    "continue",  // KwContinue
    "throw",  // KwThrow
    "try",  // KwTry
    "catch",  // KwCatch
    "finally",  // KwFinally
    "class",  // KwClass
    "interface",  // KwInterface
    "extends",  // KwExtends
    "implements",  // KwImplements
    "abstract",  // KwAbstract
    "static",  // KwStatic
    "public",  // KwPublic
    "private",  // KwPrivate
    "protected",  // KwProtected
    "enum",  // KwEnum
    "new",  // KwNew
    "this",  // KwThis
    "import",  // KwImport
    "export",  // KwExport
    "from",  // KwFrom
    "as",  // KwAs
    "native",  // KwNative
    "async",  // KwAsync
    "await",  // KwAwait
    "weak",  // KwWeak
    "ref",  // KwRef
    "typeof",  // KwTypeof
    "get",  // KwGet
    "set",  // KwSet
    "true",  // KwTrue
    "false",  // KwFalse
    "null",  // KwNull
    "undefined",  // KwUndefined
    "void",  // KwVoid
    "unknown",  // KwUnknown
    "type",  // KwType
    "any",  // KwAny
    "delete",  // KwDelete
    "yield",  // KwYield
    "(",  // LParen
    ")",  // RParen
    "{",  // LBrace
    "}",  // RBrace
    "[",  // LBracket
    "]",  // RBracket
    ";",  // Semicolon
    ",",  // Comma
    ".",  // Dot
    "..",  // DotDot
    "...",  // Ellipsis
    "=>",  // Arrow
    ":",  // Colon
    "?",  // Question
    "?.",  // QuestionDot
    "??",  // QuestionQuestion
    "+",  // Plus
    "-",  // Minus
    "*",  // Star
    "/",  // Slash
    "%",  // Percent
    "**",  // StarStar
    "++",  // PlusPlus
    "--",  // MinusMinus
    "=",  // Assign
    "+=",  // PlusAssign
    "-=",  // MinusAssign
    "*=",  // StarAssign
    "/=",  // SlashAssign
    "%=",  // PercentAssign
    "**=",  // StarStarAssign
    "&=",  // AmpersandAssign
    "|=",  // PipeAssign
    "^=",  // CaretAssign
    "<<=",  // ShiftLeftAssign
    ">>=",  // ShiftRightAssign
    ">>>=",  // UnsignedShiftRightAssign
    "&&=",  // AndAndAssign
    "||=",  // OrOrAssign
    "??=",  // QuestionQuestionAssign
    "==",  // Equal
    "!=",  // NotEqual
    "===",  // StrictEqual
    "!==",  // StrictNotEqual
    "<",  // Less
    ">",  // Greater
    "<=",  // LessEqual
    ">=",  // GreaterEqual
    "&&",  // AndAnd
    "||",  // OrOr
    "!",  // Bang
    "&",  // Ampersand
    "|",  // Pipe
    "^",  // Caret
    "~",  // Tilde
    "<<",  // ShiftLeft
    ">>",  // ShiftRight
    ">>>",  // UnsignedShiftRight
    "@",  // At
    "\\",  // Backslash
};

constexpr std::size_t kTokenKindCount =
    static_cast<std::size_t>(TokenKind::Backslash) + 1;

static_assert(sizeof(kTokenKindNames) / sizeof(kTokenKindNames[0]) ==
                  kTokenKindCount,
              "kTokenKindNames must have one entry per TokenKind");

const char* tokenKindName(TokenKind kind) noexcept {
    const std::size_t index = static_cast<std::size_t>(kind);
    return index < kTokenKindCount ? kTokenKindNames[index] : "Unknown";
}

bool isKeyword(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::KwLet: case TokenKind::KwVar: case TokenKind::KwConst:
        case TokenKind::KwFunction: case TokenKind::KwReturn:
        case TokenKind::KwIf: case TokenKind::KwElse:
        case TokenKind::KwWhile: case TokenKind::KwDo: case TokenKind::KwFor:
        case TokenKind::KwOf: case TokenKind::KwIn:
        case TokenKind::KwSwitch: case TokenKind::KwCase: case TokenKind::KwDefault:
        case TokenKind::KwBreak: case TokenKind::KwContinue:
        case TokenKind::KwThrow: case TokenKind::KwTry: case TokenKind::KwCatch:
        case TokenKind::KwFinally:
        case TokenKind::KwClass: case TokenKind::KwInterface: case TokenKind::KwExtends:
        case TokenKind::KwImplements:
        case TokenKind::KwAbstract: case TokenKind::KwStatic: case TokenKind::KwPublic:
        case TokenKind::KwPrivate: case TokenKind::KwProtected:
        case TokenKind::KwEnum: case TokenKind::KwNew: case TokenKind::KwThis:
        case TokenKind::KwImport: case TokenKind::KwExport: case TokenKind::KwFrom:
        case TokenKind::KwAs: case TokenKind::KwNative:
        case TokenKind::KwAsync: case TokenKind::KwAwait:
        case TokenKind::KwWeak: case TokenKind::KwRef:
        case TokenKind::KwTypeof:
        case TokenKind::KwGet: case TokenKind::KwSet:
        case TokenKind::KwTrue: case TokenKind::KwFalse:
        case TokenKind::KwNull: case TokenKind::KwUndefined:
        case TokenKind::KwVoid: case TokenKind::KwUnknown: case TokenKind::KwType:
        case TokenKind::KwAny: case TokenKind::KwDelete: case TokenKind::KwYield:
            return true;
        default:
            return false;
    }
}

// Keywords that may also serve as an identifier/name depending on position.
bool isContextualKeyword(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::KwOf:
        case TokenKind::KwFrom:
        case TokenKind::KwAs:
        case TokenKind::KwGet:
        case TokenKind::KwSet:
        case TokenKind::KwAsync:
        case TokenKind::KwAwait:
        case TokenKind::KwStatic:
        case TokenKind::KwType:
        case TokenKind::KwNative:
        case TokenKind::KwRef:
        case TokenKind::KwWeak:
            return true;
        default:
            return false;
    }
}

const Token& TokenStream::eofToken() {
    static const Token eof = [] {
        Token t;
        t.kind = TokenKind::EndOfFile;
        return t;
    }();
    return eof;
}

const Token& TokenStream::peek(std::size_t lookahead) const noexcept {
    std::size_t index = cursor_ + lookahead;
    if (index >= tokens_.size()) {
        return eofToken();
    }
    return tokens_[index];
}

Token TokenStream::advance() {
    if (cursor_ >= tokens_.size()) {
        return eofToken();
    }
    return tokens_[cursor_++];
}

const Token& TokenStream::previous() const noexcept {
    if (cursor_ == 0) {
        return eofToken();
    }
    return tokens_[cursor_ - 1];
}

}  // namespace cp3
