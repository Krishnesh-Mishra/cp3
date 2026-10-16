#include "cp3/token.hpp"

namespace cp3 {

const char* tokenKindName(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::EndOfFile: return "EndOfFile";
        case TokenKind::Unknown: return "Unknown";
        case TokenKind::Invalid: return "Invalid";

        case TokenKind::NumericLiteral: return "NumericLiteral";
        case TokenKind::StringLiteral: return "StringLiteral";
        case TokenKind::RegExpLiteral: return "RegExpLiteral";
        case TokenKind::TemplateString: return "TemplateString";
        case TokenKind::TemplateHead: return "TemplateHead";
        case TokenKind::TemplateMiddle: return "TemplateMiddle";
        case TokenKind::TemplateTail: return "TemplateTail";

        case TokenKind::Identifier: return "Identifier";
        case TokenKind::PrivateIdentifier: return "PrivateIdentifier";

        case TokenKind::LineComment: return "LineComment";
        case TokenKind::BlockComment: return "BlockComment";

        case TokenKind::KwLet: return "let";
        case TokenKind::KwVar: return "var";
        case TokenKind::KwConst: return "const";
        case TokenKind::KwFunction: return "function";
        case TokenKind::KwReturn: return "return";
        case TokenKind::KwIf: return "if";
        case TokenKind::KwElse: return "else";
        case TokenKind::KwWhile: return "while";
        case TokenKind::KwDo: return "do";
        case TokenKind::KwFor: return "for";
        case TokenKind::KwOf: return "of";
        case TokenKind::KwIn: return "in";
        case TokenKind::KwSwitch: return "switch";
        case TokenKind::KwCase: return "case";
        case TokenKind::KwDefault: return "default";
        case TokenKind::KwBreak: return "break";
        case TokenKind::KwContinue: return "continue";
        case TokenKind::KwThrow: return "throw";
        case TokenKind::KwTry: return "try";
        case TokenKind::KwCatch: return "catch";
        case TokenKind::KwFinally: return "finally";
        case TokenKind::KwClass: return "class";
        case TokenKind::KwInterface: return "interface";
        case TokenKind::KwExtends: return "extends";
        case TokenKind::KwImplements: return "implements";
        case TokenKind::KwAbstract: return "abstract";
        case TokenKind::KwStatic: return "static";
        case TokenKind::KwPublic: return "public";
        case TokenKind::KwPrivate: return "private";
        case TokenKind::KwProtected: return "protected";
        case TokenKind::KwEnum: return "enum";
        case TokenKind::KwNew: return "new";
        case TokenKind::KwThis: return "this";
        case TokenKind::KwImport: return "import";
        case TokenKind::KwExport: return "export";
        case TokenKind::KwFrom: return "from";
        case TokenKind::KwAs: return "as";
        case TokenKind::KwNative: return "native";
        case TokenKind::KwAsync: return "async";
        case TokenKind::KwAwait: return "await";
        case TokenKind::KwWeak: return "weak";
        case TokenKind::KwRef: return "ref";
        case TokenKind::KwTypeof: return "typeof";
        case TokenKind::KwGet: return "get";
        case TokenKind::KwSet: return "set";
        case TokenKind::KwTrue: return "true";
        case TokenKind::KwFalse: return "false";
        case TokenKind::KwNull: return "null";
        case TokenKind::KwUndefined: return "undefined";
        case TokenKind::KwVoid: return "void";
        case TokenKind::KwUnknown: return "unknown";
        case TokenKind::KwType: return "type";
        case TokenKind::KwAny: return "any";
        case TokenKind::KwDelete: return "delete";
        case TokenKind::KwYield: return "yield";

        case TokenKind::LParen: return "(";
        case TokenKind::RParen: return ")";
        case TokenKind::LBrace: return "{";
        case TokenKind::RBrace: return "}";
        case TokenKind::LBracket: return "[";
        case TokenKind::RBracket: return "]";
        case TokenKind::Semicolon: return ";";
        case TokenKind::Comma: return ",";
        case TokenKind::Dot: return ".";
        case TokenKind::DotDot: return "..";
        case TokenKind::Ellipsis: return "...";
        case TokenKind::Arrow: return "=>";
        case TokenKind::Colon: return ":";
        case TokenKind::Question: return "?";
        case TokenKind::QuestionDot: return "?.";
        case TokenKind::QuestionQuestion: return "??";

        case TokenKind::Plus: return "+";
        case TokenKind::Minus: return "-";
        case TokenKind::Star: return "*";
        case TokenKind::Slash: return "/";
        case TokenKind::Percent: return "%";
        case TokenKind::StarStar: return "**";
        case TokenKind::PlusPlus: return "++";
        case TokenKind::MinusMinus: return "--";
        case TokenKind::Assign: return "=";
        case TokenKind::PlusAssign: return "+=";
        case TokenKind::MinusAssign: return "-=";
        case TokenKind::StarAssign: return "*=";
        case TokenKind::SlashAssign: return "/=";
        case TokenKind::PercentAssign: return "%=";
        case TokenKind::StarStarAssign: return "**=";
        case TokenKind::AmpersandAssign: return "&=";
        case TokenKind::PipeAssign: return "|=";
        case TokenKind::CaretAssign: return "^=";
        case TokenKind::ShiftLeftAssign: return "<<=";
        case TokenKind::ShiftRightAssign: return ">>=";
        case TokenKind::AndAndAssign: return "&&=";
        case TokenKind::OrOrAssign: return "||=";
        case TokenKind::QuestionQuestionAssign: return "??=";
        case TokenKind::Equal: return "==";
        case TokenKind::NotEqual: return "!=";
        case TokenKind::StrictEqual: return "===";
        case TokenKind::StrictNotEqual: return "!==";
        case TokenKind::Less: return "<";
        case TokenKind::Greater: return ">";
        case TokenKind::LessEqual: return "<=";
        case TokenKind::GreaterEqual: return ">=";
        case TokenKind::AndAnd: return "&&";
        case TokenKind::OrOr: return "||";
        case TokenKind::Bang: return "!";
        case TokenKind::Ampersand: return "&";
        case TokenKind::Pipe: return "|";
        case TokenKind::Caret: return "^";
        case TokenKind::Tilde: return "~";
        case TokenKind::ShiftLeft: return "<<";
        case TokenKind::ShiftRight: return ">>";
        case TokenKind::UnsignedShiftRight: return ">>>";

        case TokenKind::At: return "@";
        case TokenKind::Backslash: return "\\";
    }
    return "Unknown";
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
