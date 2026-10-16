// =============================================================================
// cp3/lexer.cpp — the C+++ (.cp3) scanner.
//
// Implements the frozen `Lexer` boundary declared in cp3/lexer.hpp: construct
// over a SourceFile + DiagnosticSink, and produce a TokenStream. Every lexical
// problem is reported through the sink (CP3101-CP3107); the scanner never
// throws, always recovers, and always ends the stream with an EndOfFile token.
//
// Template literals follow DECISIONS.md D7: the scanner consumes a template
// literal *fully* using an internal mode stack, emitting
//   TemplateHead, <interpolation tokens>, TemplateMiddle..., TemplateTail
// or a single TemplateString when there is no substitution. Nested templates
// inside `${ ... }` are supported. There is no parser->lexer callback.
//
// Positions come from SourceFile::positionAt (1-based line/column, byte
// columns, 0-based offsets) via SourceManager::makeSpan.
// =============================================================================
#include "cp3/lexer.hpp"

#include <cstdlib>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"

namespace cp3 {
namespace {

// -----------------------------------------------------------------------------
// Character classification (byte-oriented; cp3 source is UTF-8 and any byte
// >= 0x80 is treated as an identifier character, which is a pragmatic and safe
// choice for a byte-column lexer).
// -----------------------------------------------------------------------------
bool isDecDigit(unsigned char c) noexcept { return c >= '0' && c <= '9'; }
bool isBinDigit(unsigned char c) noexcept { return c == '0' || c == '1'; }
bool isOctDigit(unsigned char c) noexcept { return c >= '0' && c <= '7'; }
bool isHexDigit(unsigned char c) noexcept {
    return isDecDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
int hexVal(unsigned char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}
bool isIdentStart(unsigned char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == '$' || c >= 0x80;
}
bool isIdentContinue(unsigned char c) noexcept {
    return isIdentStart(c) || isDecDigit(c);
}

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

TokenKind keywordFor(std::string_view s) noexcept {
    static const std::unordered_map<std::string_view, TokenKind> kKeywords = {
        {"let", TokenKind::KwLet},         {"var", TokenKind::KwVar},
        {"const", TokenKind::KwConst},     {"function", TokenKind::KwFunction},
        {"return", TokenKind::KwReturn},   {"if", TokenKind::KwIf},
        {"else", TokenKind::KwElse},       {"while", TokenKind::KwWhile},
        {"do", TokenKind::KwDo},           {"for", TokenKind::KwFor},
        {"of", TokenKind::KwOf},           {"in", TokenKind::KwIn},
        {"switch", TokenKind::KwSwitch},   {"case", TokenKind::KwCase},
        {"default", TokenKind::KwDefault}, {"break", TokenKind::KwBreak},
        {"continue", TokenKind::KwContinue},
        {"throw", TokenKind::KwThrow},     {"try", TokenKind::KwTry},
        {"catch", TokenKind::KwCatch},     {"finally", TokenKind::KwFinally},
        {"class", TokenKind::KwClass},     {"interface", TokenKind::KwInterface},
        {"extends", TokenKind::KwExtends}, {"implements", TokenKind::KwImplements},
        {"abstract", TokenKind::KwAbstract},
        {"static", TokenKind::KwStatic},   {"public", TokenKind::KwPublic},
        {"private", TokenKind::KwPrivate}, {"protected", TokenKind::KwProtected},
        {"enum", TokenKind::KwEnum},       {"new", TokenKind::KwNew},
        {"this", TokenKind::KwThis},       {"import", TokenKind::KwImport},
        {"export", TokenKind::KwExport},   {"from", TokenKind::KwFrom},
        {"as", TokenKind::KwAs},           {"native", TokenKind::KwNative},
        {"async", TokenKind::KwAsync},     {"await", TokenKind::KwAwait},
        {"weak", TokenKind::KwWeak},       {"ref", TokenKind::KwRef},
        {"typeof", TokenKind::KwTypeof},   {"get", TokenKind::KwGet},
        {"set", TokenKind::KwSet},         {"true", TokenKind::KwTrue},
        {"false", TokenKind::KwFalse},     {"null", TokenKind::KwNull},
        {"undefined", TokenKind::KwUndefined},
        {"void", TokenKind::KwVoid},       {"unknown", TokenKind::KwUnknown},
        {"type", TokenKind::KwType},       {"any", TokenKind::KwAny},
        {"delete", TokenKind::KwDelete},   {"yield", TokenKind::KwYield},
    };
    auto it = kKeywords.find(s);
    return it == kKeywords.end() ? TokenKind::Identifier : it->second;
}

// -----------------------------------------------------------------------------
// Scanner
// -----------------------------------------------------------------------------
class Scanner {
public:
    Scanner(const SourceFile& file, DiagnosticSink& sink) noexcept
        : file_(file), sink_(sink), text_(file.text),
          n_(static_cast<std::size_t>(file.text.size())) {}

    TokenStream run() {
        while (i_ < n_) {
            skipTrivia();
            if (i_ >= n_) break;
            scanToken();
        }
        if (!templateDepth_.empty()) {
            errorAt(diagcode::kUnterminatedTemplate, static_cast<std::uint32_t>(n_),
                    static_cast<std::uint32_t>(n_),
                    "unterminated template literal");
        }
        // EndOfFile always terminates the stream.
        const std::uint32_t end = static_cast<std::uint32_t>(n_);
        Token eof;
        eof.kind = TokenKind::EndOfFile;
        eof.span = SourceManager::makeSpan(&file_, end, end);
        eof.precededByNewline = pendingNewline_;
        tokens_.push_back(std::move(eof));
        return TokenStream(std::move(tokens_));
    }

private:
    // -- helpers ---------------------------------------------------------------
    char peek(std::size_t k = 0) const noexcept {
        return (i_ + k < n_) ? text_[i_ + k] : '\0';
    }
    SourceSpan span(std::uint32_t b, std::uint32_t e) const {
        return SourceManager::makeSpan(&file_, b, e);
    }
    void errorAt(const char* code, std::uint32_t b, std::uint32_t e, std::string msg) {
        sink_.error(code, span(b, e), std::move(msg));
    }
    void commit(Token t) {
        t.precededByNewline = pendingNewline_;
        pendingNewline_ = false;
        prev_ = t.kind;
        hasPrev_ = true;
        tokens_.push_back(std::move(t));
    }
    void emitSimple(TokenKind kind, std::uint32_t len) {
        const std::uint32_t start = static_cast<std::uint32_t>(i_);
        i_ += len;
        Token t;
        t.kind = kind;
        t.span = span(start, static_cast<std::uint32_t>(i_));
        t.lexeme = file_.slice(t.span);
        commit(std::move(t));
    }

    bool isLineTermAt(std::size_t i) const noexcept {
        if (i >= n_) return false;
        const unsigned char c = static_cast<unsigned char>(text_[i]);
        if (c == '\n' || c == '\r') return true;
        return c == 0xE2 && i + 2 < n_ &&
               static_cast<unsigned char>(text_[i + 1]) == 0x80 &&
               (static_cast<unsigned char>(text_[i + 2]) == 0xA8 ||
                static_cast<unsigned char>(text_[i + 2]) == 0xA9);
    }

    // True when `/` at the current position should begin a RegExp literal
    // rather than a division: the previous *significant* token cannot end an
    // expression. This is the standard "previous token" heuristic.
    bool regexAllowed() const noexcept {
        if (!hasPrev_) return true;
        switch (prev_) {
            case TokenKind::Identifier:
            case TokenKind::PrivateIdentifier:
            case TokenKind::NumericLiteral:
            case TokenKind::StringLiteral:
            case TokenKind::RegExpLiteral:
            case TokenKind::TemplateString:
            case TokenKind::TemplateTail:
            case TokenKind::RParen:
            case TokenKind::RBracket:
            case TokenKind::RBrace:
            case TokenKind::PlusPlus:
            case TokenKind::MinusMinus:
            case TokenKind::KwTrue:
            case TokenKind::KwFalse:
            case TokenKind::KwNull:
            case TokenKind::KwUndefined:
            case TokenKind::KwThis:
                return false;
            default:
                return true;
        }
    }

    // -- trivia ----------------------------------------------------------------
    void skipTrivia() {
        bool newline = false;
        while (i_ < n_) {
            const unsigned char c = static_cast<unsigned char>(text_[i_]);
            if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
                ++i_;
                continue;
            }
            if (c == '\n') {
                ++i_;
                newline = true;
                continue;
            }
            if (c == '\r') {
                ++i_;
                if (i_ < n_ && text_[i_] == '\n') ++i_;
                newline = true;
                continue;
            }
            // U+2028 / U+2029 (line/paragraph separator).
            if (c == 0xE2 && i_ + 2 < n_ &&
                static_cast<unsigned char>(text_[i_ + 1]) == 0x80 &&
                (static_cast<unsigned char>(text_[i_ + 2]) == 0xA8 ||
                 static_cast<unsigned char>(text_[i_ + 2]) == 0xA9)) {
                i_ += 3;
                newline = true;
                continue;
            }
            // U+00A0 NBSP and U+FEFF BOM count as whitespace.
            if (c == 0xC2 && i_ + 1 < n_ &&
                static_cast<unsigned char>(text_[i_ + 1]) == 0xA0) {
                i_ += 2;
                continue;
            }
            if (c == 0xEF && i_ + 2 < n_ &&
                static_cast<unsigned char>(text_[i_ + 1]) == 0xBB &&
                static_cast<unsigned char>(text_[i_ + 2]) == 0xBF) {
                i_ += 3;
                continue;
            }
            if (c == '/' && i_ + 1 < n_ && text_[i_ + 1] == '/') {
                i_ += 2;
                while (i_ < n_ && !isLineTermAt(i_)) ++i_;
                continue;
            }
            if (c == '/' && i_ + 1 < n_ && text_[i_ + 1] == '*') {
                const std::uint32_t cs = static_cast<std::uint32_t>(i_);
                i_ += 2;
                bool closed = false;
                while (i_ < n_) {
                    if (text_[i_] == '*' && i_ + 1 < n_ && text_[i_ + 1] == '/') {
                        i_ += 2;
                        closed = true;
                        break;
                    }
                    if (isLineTermAt(i_)) newline = true;
                    ++i_;
                }
                if (!closed) {
                    errorAt(diagcode::kUnterminatedComment, cs,
                            static_cast<std::uint32_t>(i_),
                            "unterminated block comment");
                }
                continue;
            }
            break;
        }
        if (newline) pendingNewline_ = true;
    }

    // -- token dispatch --------------------------------------------------------
    void scanToken() {
        const char c = text_[i_];

        // Closing `}` of a template interpolation resumes template scanning
        // instead of producing an RBrace token.
        if (!templateDepth_.empty() && c == '}') {
            if (templateDepth_.back() == 0) {
                templateDepth_.pop_back();
                scanTemplateChunk(true);
                return;
            }
            --templateDepth_.back();
            emitSimple(TokenKind::RBrace, 1);
            return;
        }

        if (c == '`') {
            scanTemplateChunk(false);
            return;
        }

        if (isIdentStart(static_cast<unsigned char>(c))) {
            scanIdentifier();
            return;
        }

        if (isDecDigit(static_cast<unsigned char>(c)) ||
            (c == '.' && isDecDigit(static_cast<unsigned char>(peek(1))))) {
            scanNumber();
            return;
        }

        if (c == '"' || c == '\'') {
            scanString(c);
            return;
        }

        if (c == '#') {
            scanPrivateIdentifier();
            return;
        }

        if (c == '/' && regexAllowed()) {
            scanRegex();
            return;
        }

        scanPunctuation(c);
    }

    // -- identifiers -----------------------------------------------------------
    std::size_t scanIdentChars() {
        while (i_ < n_ && isIdentContinue(static_cast<unsigned char>(text_[i_]))) ++i_;
        return i_;
    }

    void scanIdentifier() {
        const std::uint32_t start = static_cast<std::uint32_t>(i_);
        ++i_;
        scanIdentChars();
        const std::uint32_t end = static_cast<std::uint32_t>(i_);
        const std::string_view lex = text_.substr(start, end - start);
        Token t;
        t.kind = keywordFor(lex);
        t.span = span(start, end);
        t.lexeme = file_.slice(t.span);
        commit(std::move(t));
    }

    void scanPrivateIdentifier() {
        const std::uint32_t start = static_cast<std::uint32_t>(i_);
        if (!(peek(1) != '\0' && isIdentStart(static_cast<unsigned char>(peek(1))))) {
            // A bare `#` is not a valid character.
            ++i_;
            errorAt(diagcode::kInvalidCharacter, start, static_cast<std::uint32_t>(i_),
                    "unexpected character '#'");
            Token t;
            t.kind = TokenKind::Invalid;
            t.span = span(start, static_cast<std::uint32_t>(i_));
            t.lexeme = file_.slice(t.span);
            commit(std::move(t));
            return;
        }
        ++i_;
        scanIdentChars();
        Token t;
        t.kind = TokenKind::PrivateIdentifier;
        t.span = span(start, static_cast<std::uint32_t>(i_));
        t.lexeme = file_.slice(t.span);
        commit(std::move(t));
    }

    // -- numbers ---------------------------------------------------------------
    // Scans a run of digits of one radix allowing `_` separators strictly
    // *between* two digits; appends accepted digits to `clean`.
    bool scanDigitRun(bool (*isDigit)(unsigned char), std::string& clean, bool& sepError) {
        bool saw = false;
        bool prevDigit = false;
        while (i_ < n_) {
            const unsigned char c = static_cast<unsigned char>(text_[i_]);
            if (isDigit(c)) {
                clean.push_back(static_cast<char>(c));
                saw = true;
                prevDigit = true;
                ++i_;
            } else if (c == '_') {
                const bool ok = prevDigit && i_ + 1 < n_ &&
                                isDigit(static_cast<unsigned char>(text_[i_ + 1]));
                if (!ok) sepError = true;
                ++i_;
                prevDigit = false;
            } else {
                break;
            }
        }
        return saw;
    }

    void scanNumber() {
        const std::uint32_t start = static_cast<std::uint32_t>(i_);
        bool error = false;
        double value = 0.0;

        const bool hasPrefix =
            text_[i_] == '0' && i_ + 1 < n_ &&
            (text_[i_ + 1] == 'x' || text_[i_ + 1] == 'X' || text_[i_ + 1] == 'b' ||
             text_[i_ + 1] == 'B' || text_[i_ + 1] == 'o' || text_[i_ + 1] == 'O');

        if (hasPrefix) {
            const char p = text_[i_ + 1];
            i_ += 2;
            std::string digits;
            bool sepError = false;
            bool seen = false;
            if (p == 'x' || p == 'X') {
                seen = scanDigitRun(isHexDigit, digits, sepError);
            } else if (p == 'b' || p == 'B') {
                seen = scanDigitRun(isBinDigit, digits, sepError);
            } else {
                seen = scanDigitRun(isOctDigit, digits, sepError);
            }
            if (!seen) error = true;
            if (sepError) error = true;

            if (p == 'x' || p == 'X') {
                if (!digits.empty()) {
                    const std::string hexText = "0x" + digits;
                    value = std::strtod(hexText.c_str(), nullptr);
                }
            } else {
                const int radix = (p == 'b' || p == 'B') ? 2 : 8;
                for (char d : digits) value = value * radix + (d - '0');
            }

            // A digit that is not valid for this radix, or an identifier that
            // immediately follows, is malformed (`0b12`, `0x1Fg`).
            if (i_ < n_ && (isIdentStart(static_cast<unsigned char>(text_[i_])) ||
                            isDecDigit(static_cast<unsigned char>(text_[i_])))) {
                error = true;
                while (i_ < n_ &&
                       (isIdentContinue(static_cast<unsigned char>(text_[i_])) ||
                        text_[i_] == '_')) {
                    ++i_;
                }
            }
        } else {
            // Legacy octal (`08`, `0_1`) is rejected; cp3 has no implicit octal.
            if (text_[i_] == '0' && i_ + 1 < n_ &&
                (isDecDigit(static_cast<unsigned char>(text_[i_ + 1])) || text_[i_ + 1] == '_')) {
                error = true;
            }

            std::string clean;
            bool sepError = false;
            scanDigitRun(isDecDigit, clean, sepError);

            if (i_ < n_ && text_[i_] == '.' &&
                !(i_ + 1 < n_ && text_[i_ + 1] == '.')) {
                clean.push_back('.');
                ++i_;
                scanDigitRun(isDecDigit, clean, sepError);
            }

            if (i_ < n_ && (text_[i_] == 'e' || text_[i_] == 'E')) {
                ++i_;
                std::string exp;
                if (i_ < n_ && (text_[i_] == '+' || text_[i_] == '-')) {
                    exp.push_back(text_[i_]);
                    ++i_;
                }
                std::string expDigits;
                bool expSep = false;
                const bool hadDigits = scanDigitRun(isDecDigit, expDigits, expSep);
                if (!hadDigits) {
                    error = true;
                } else {
                    clean.push_back('e');
                    clean += exp;
                    clean += expDigits;
                }
                if (expSep) error = true;
            }

            if (sepError) error = true;

            if (!clean.empty()) value = std::strtod(clean.c_str(), nullptr);

            if (i_ < n_ && isIdentStart(static_cast<unsigned char>(text_[i_]))) {
                error = true;
                while (i_ < n_ &&
                       (isIdentContinue(static_cast<unsigned char>(text_[i_])) ||
                        text_[i_] == '_')) {
                    ++i_;
                }
            }
        }

        Token t;
        t.kind = TokenKind::NumericLiteral;
        t.span = span(start, static_cast<std::uint32_t>(i_));
        t.lexeme = file_.slice(t.span);
        t.numberValue = value;
        if (error) {
            sink_.error(diagcode::kInvalidNumericLiteral, t.span,
                        "invalid numeric literal");
        }
        commit(std::move(t));
    }

    // -- string / escape -------------------------------------------------------
    // Decodes one escape sequence beginning at the backslash; appends the
    // cooked result to `out`. Reports CP3106 for malformed escapes.
    void scanEscape(std::string& out, bool inTemplate) {
        const std::uint32_t escStart = static_cast<std::uint32_t>(i_);
        ++i_;  // consume backslash
        if (i_ >= n_) {
            errorAt(diagcode::kInvalidEscape, escStart, static_cast<std::uint32_t>(i_),
                    "invalid escape sequence at end of file");
            return;
        }
        const unsigned char c = static_cast<unsigned char>(text_[i_]);
        switch (c) {
            case 'n': out.push_back('\n'); ++i_; return;
            case 't': out.push_back('\t'); ++i_; return;
            case 'r': out.push_back('\r'); ++i_; return;
            case 'b': out.push_back('\b'); ++i_; return;
            case 'f': out.push_back('\f'); ++i_; return;
            case 'v': out.push_back('\v'); ++i_; return;
            case '\'': out.push_back('\''); ++i_; return;
            case '"': out.push_back('"'); ++i_; return;
            case '\\': out.push_back('\\'); ++i_; return;
            case '/': out.push_back('/'); ++i_; return;
            case '`':
                if (inTemplate) { out.push_back('`'); ++i_; return; }
                break;
            case '$':
                if (inTemplate) { out.push_back('$'); ++i_; return; }
                break;
            case '0': {
                if (i_ + 1 < n_ && isDecDigit(static_cast<unsigned char>(text_[i_ + 1]))) {
                    ++i_;
                    errorAt(diagcode::kInvalidEscape, escStart,
                            static_cast<std::uint32_t>(i_),
                            "legacy octal escape sequences are not allowed");
                    return;
                }
                out.push_back('\0');
                ++i_;
                return;
            }
            case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9': {
                ++i_;
                while (i_ < n_ && isDecDigit(static_cast<unsigned char>(text_[i_]))) ++i_;
                errorAt(diagcode::kInvalidEscape, escStart,
                        static_cast<std::uint32_t>(i_),
                        "legacy octal escape sequences are not allowed");
                return;
            }
            case 'x': {
                ++i_;
                int count = 0;
                unsigned v = 0;
                while (count < 2 && i_ < n_ &&
                       isHexDigit(static_cast<unsigned char>(text_[i_]))) {
                    v = v * 16 + static_cast<unsigned>(hexVal(
                                     static_cast<unsigned char>(text_[i_])));
                    ++i_;
                    ++count;
                }
                if (count < 2) {
                    errorAt(diagcode::kInvalidEscape, escStart,
                            static_cast<std::uint32_t>(i_),
                            "invalid hexadecimal escape sequence");
                }
                out.push_back(static_cast<char>(v));
                return;
            }
            case 'u': {
                ++i_;
                if (i_ < n_ && text_[i_] == '{') {
                    ++i_;
                    std::uint32_t cp = 0;
                    int count = 0;
                    while (i_ < n_ && isHexDigit(static_cast<unsigned char>(text_[i_]))) {
                        cp = cp * 16 + static_cast<std::uint32_t>(hexVal(
                                           static_cast<unsigned char>(text_[i_])));
                        ++i_;
                        ++count;
                    }
                    if (count == 0 || i_ >= n_ || text_[i_] != '}') {
                        errorAt(diagcode::kInvalidEscape, escStart,
                                static_cast<std::uint32_t>(i_),
                                "invalid Unicode escape sequence");
                    } else {
                        ++i_;  // consume '}'
                        if (cp > 0x10FFFF) {
                            errorAt(diagcode::kInvalidEscape, escStart,
                                    static_cast<std::uint32_t>(i_),
                                    "Unicode code point is out of range");
                        } else {
                            appendUtf8(out, cp);
                        }
                    }
                } else {
                    std::uint32_t cp = 0;
                    int count = 0;
                    while (count < 4 && i_ < n_ &&
                           isHexDigit(static_cast<unsigned char>(text_[i_]))) {
                        cp = cp * 16 + static_cast<std::uint32_t>(hexVal(
                                           static_cast<unsigned char>(text_[i_])));
                        ++i_;
                        ++count;
                    }
                    if (count < 4) {
                        errorAt(diagcode::kInvalidEscape, escStart,
                                static_cast<std::uint32_t>(i_),
                                "invalid Unicode escape sequence");
                    } else {
                        appendUtf8(out, cp);
                    }
                }
                return;
            }
            default:
                break;
        }

        // Line continuation: backslash + line terminator contributes nothing.
        if (c == '\n' || c == '\r') {
            ++i_;
            if (c == '\r' && i_ < n_ && text_[i_] == '\n') ++i_;
            return;
        }
        if (c == 0xE2 && i_ + 2 < n_ &&
            static_cast<unsigned char>(text_[i_ + 1]) == 0x80 &&
            (static_cast<unsigned char>(text_[i_ + 2]) == 0xA8 ||
             static_cast<unsigned char>(text_[i_ + 2]) == 0xA9)) {
            i_ += 3;
            return;
        }

        // Unknown escape character: reject, but recover by emitting it.
        errorAt(diagcode::kInvalidEscape, escStart,
                static_cast<std::uint32_t>(i_ + 1), "invalid escape sequence");
        out.push_back(static_cast<char>(c));
        ++i_;
    }

    void scanString(char quote) {
        const std::uint32_t start = static_cast<std::uint32_t>(i_);
        ++i_;  // opening quote
        std::string value;
        bool terminated = false;
        while (i_ < n_) {
            const unsigned char c = static_cast<unsigned char>(text_[i_]);
            if (c == static_cast<unsigned char>(quote)) {
                ++i_;
                terminated = true;
                break;
            }
            if (c == '\\') {
                scanEscape(value, false);
                continue;
            }
            if (isLineTermAt(i_)) break;  // raw newline ends the literal
            value.push_back(static_cast<char>(c));
            ++i_;
        }
        Token t;
        t.kind = TokenKind::StringLiteral;
        t.span = span(start, static_cast<std::uint32_t>(i_));
        t.lexeme = file_.slice(t.span);
        t.stringValue = std::move(value);
        if (!terminated) {
            t.unterminated = true;
            errorAt(diagcode::kUnterminatedString, start, static_cast<std::uint32_t>(i_),
                    "unterminated string literal");
        }
        commit(std::move(t));
    }

    // -- templates -------------------------------------------------------------
    // Scans one template chunk. `continuing == false` when the chunk starts
    // with a backtick (TemplateHead / TemplateString); `true` when it starts
    // with the `}` that closed an interpolation (TemplateMiddle / TemplateTail).
    void scanTemplateChunk(bool continuing) {
        const std::uint32_t start = static_cast<std::uint32_t>(i_);
        ++i_;  // consume ` or }
        const std::uint32_t rawStart = static_cast<std::uint32_t>(i_);
        std::string cooked;
        bool terminated = false;
        bool sawInterp = false;
        std::uint32_t contentEnd = rawStart;

        while (i_ < n_) {
            const unsigned char c = static_cast<unsigned char>(text_[i_]);
            if (c == '`') {
                contentEnd = static_cast<std::uint32_t>(i_);
                ++i_;
                terminated = true;
                break;
            }
            if (c == '\\') {
                scanEscape(cooked, true);
                continue;
            }
            if (c == '$' && i_ + 1 < n_ && text_[i_ + 1] == '{') {
                contentEnd = static_cast<std::uint32_t>(i_);
                i_ += 2;
                sawInterp = true;
                break;
            }
            if (c == '\r') {
                cooked.push_back('\n');
                ++i_;
                if (i_ < n_ && text_[i_] == '\n') ++i_;
                continue;
            }
            if (c == '\n') {
                cooked.push_back('\n');
                ++i_;
                continue;
            }
            cooked.push_back(static_cast<char>(c));
            ++i_;
        }
        if (!terminated && !sawInterp) contentEnd = static_cast<std::uint32_t>(i_);

        TokenKind kind;
        if (!continuing) {
            kind = sawInterp ? TokenKind::TemplateHead : TokenKind::TemplateString;
        } else {
            kind = sawInterp ? TokenKind::TemplateMiddle : TokenKind::TemplateTail;
        }

        Token t;
        t.kind = kind;
        t.span = span(start, static_cast<std::uint32_t>(i_));
        t.lexeme = file_.slice(t.span);
        t.stringValue = std::move(cooked);
        t.rawText =
            std::string(text_.substr(rawStart, contentEnd - rawStart));
        if (!terminated && !sawInterp) {
            t.unterminated = true;
            errorAt(diagcode::kUnterminatedTemplate, start,
                    static_cast<std::uint32_t>(i_), "unterminated template literal");
        }
        if (sawInterp) templateDepth_.push_back(0);
        commit(std::move(t));
    }

    // -- regex -----------------------------------------------------------------
    void scanRegex() {
        const std::uint32_t start = static_cast<std::uint32_t>(i_);
        ++i_;  // opening '/'
        bool inClass = false;
        bool closed = false;
        bool error = false;
        while (i_ < n_) {
            const unsigned char c = static_cast<unsigned char>(text_[i_]);
            if (isLineTermAt(i_)) break;  // regex cannot span lines
            if (c == '\\') {
                if (i_ + 1 < n_ && !isLineTermAt(i_ + 1)) {
                    i_ += 2;
                    continue;
                }
                error = true;
                ++i_;
                break;
            }
            if (c == '[') {
                inClass = true;
                ++i_;
                continue;
            }
            if (c == ']') {
                inClass = false;
                ++i_;
                continue;
            }
            if (c == '/' && !inClass) {
                ++i_;
                closed = true;
                break;
            }
            ++i_;
        }

        const std::uint32_t patternEnd =
            closed ? static_cast<std::uint32_t>(i_ - 1) : static_cast<std::uint32_t>(i_);
        const std::uint32_t flagsStart = static_cast<std::uint32_t>(i_);
        if (closed) {
            while (i_ < n_ && isIdentContinue(static_cast<unsigned char>(text_[i_]))) ++i_;
        }
        const std::uint32_t end = static_cast<std::uint32_t>(i_);

        if (!closed) {
            error = true;
            errorAt(diagcode::kInvalidRegExp, start, end,
                    "unterminated regular expression literal");
        }

        Token t;
        t.kind = TokenKind::RegExpLiteral;
        t.span = span(start, end);
        t.lexeme = file_.slice(t.span);
        // No dedicated regex fields exist on Token: pattern goes in
        // stringValue and the flag letters in rawText.
        t.stringValue = std::string(text_.substr(start + 1, patternEnd - (start + 1)));
        t.rawText = std::string(text_.substr(flagsStart, end - flagsStart));

        // Validate flags: a fixed set, no duplicates.
        bool seenFlags[256] = {false};
        for (std::uint32_t p = flagsStart; p < end; ++p) {
            const unsigned char f = static_cast<unsigned char>(text_[p]);
            const bool known = f == 'd' || f == 'g' || f == 'i' || f == 'm' ||
                               f == 's' || f == 'u' || f == 'v' || f == 'y';
            if (!known) {
                error = true;
                errorAt(diagcode::kInvalidRegExp, p, p + 1,
                        std::string("invalid regular expression flag '") +
                            static_cast<char>(f) + "'");
            } else if (seenFlags[f]) {
                error = true;
                errorAt(diagcode::kInvalidRegExp, p, p + 1,
                        std::string("duplicate regular expression flag '") +
                            static_cast<char>(f) + "'");
            }
            seenFlags[f] = true;
        }
        (void)error;
        commit(std::move(t));
    }

    // -- punctuation -----------------------------------------------------------
    void scanPunctuation(char c) {
        const std::uint32_t start = static_cast<std::uint32_t>(i_);
        switch (c) {
            case '(': emitSimple(TokenKind::LParen, 1); return;
            case ')': emitSimple(TokenKind::RParen, 1); return;
            case '{':
                if (!templateDepth_.empty()) ++templateDepth_.back();
                emitSimple(TokenKind::LBrace, 1);
                return;
            case '}': emitSimple(TokenKind::RBrace, 1); return;
            case '[': emitSimple(TokenKind::LBracket, 1); return;
            case ']': emitSimple(TokenKind::RBracket, 1); return;
            case ';': emitSimple(TokenKind::Semicolon, 1); return;
            case ',': emitSimple(TokenKind::Comma, 1); return;
            case ':': emitSimple(TokenKind::Colon, 1); return;
            case '~': emitSimple(TokenKind::Tilde, 1); return;
            case '@': emitSimple(TokenKind::At, 1); return;

            case '.': {
                if (peek(1) == '.' && peek(2) == '.') {
                    emitSimple(TokenKind::Ellipsis, 3);
                } else if (peek(1) == '.') {
                    emitSimple(TokenKind::DotDot, 2);
                } else {
                    emitSimple(TokenKind::Dot, 1);
                }
                return;
            }
            case '?': {
                if (peek(1) == '?' && peek(2) == '=') {
                    emitSimple(TokenKind::QuestionQuestionAssign, 3);
                } else if (peek(1) == '?') {
                    emitSimple(TokenKind::QuestionQuestion, 2);
                } else if (peek(1) == '.' &&
                           !isDecDigit(static_cast<unsigned char>(peek(2)))) {
                    emitSimple(TokenKind::QuestionDot, 2);
                } else {
                    emitSimple(TokenKind::Question, 1);
                }
                return;
            }
            case '=': {
                if (peek(1) == '=' && peek(2) == '=') {
                    emitSimple(TokenKind::StrictEqual, 3);
                } else if (peek(1) == '=') {
                    emitSimple(TokenKind::Equal, 2);
                } else if (peek(1) == '>') {
                    emitSimple(TokenKind::Arrow, 2);
                } else {
                    emitSimple(TokenKind::Assign, 1);
                }
                return;
            }
            case '!': {
                if (peek(1) == '=' && peek(2) == '=') {
                    emitSimple(TokenKind::StrictNotEqual, 3);
                } else if (peek(1) == '=') {
                    emitSimple(TokenKind::NotEqual, 2);
                } else {
                    emitSimple(TokenKind::Bang, 1);
                }
                return;
            }
            case '<': {
                if (peek(1) == '<' && peek(2) == '=') {
                    emitSimple(TokenKind::ShiftLeftAssign, 3);
                } else if (peek(1) == '<') {
                    emitSimple(TokenKind::ShiftLeft, 2);
                } else if (peek(1) == '=') {
                    emitSimple(TokenKind::LessEqual, 2);
                } else {
                    emitSimple(TokenKind::Less, 1);
                }
                return;
            }
            case '>': {
                // NOTE: `>>>=` has no TokenKind in the frozen token.hpp; it is
                // lexed as `UnsignedShiftRight` followed by `Assign`. See the
                // header gap reported with this module.
                if (peek(1) == '>' && peek(2) == '>') {
                    emitSimple(TokenKind::UnsignedShiftRight, 3);
                } else if (peek(1) == '>' && peek(2) == '=') {
                    emitSimple(TokenKind::ShiftRightAssign, 3);
                } else if (peek(1) == '>') {
                    emitSimple(TokenKind::ShiftRight, 2);
                } else if (peek(1) == '=') {
                    emitSimple(TokenKind::GreaterEqual, 2);
                } else {
                    emitSimple(TokenKind::Greater, 1);
                }
                return;
            }
            case '+': {
                if (peek(1) == '+') {
                    emitSimple(TokenKind::PlusPlus, 2);
                } else if (peek(1) == '=') {
                    emitSimple(TokenKind::PlusAssign, 2);
                } else {
                    emitSimple(TokenKind::Plus, 1);
                }
                return;
            }
            case '-': {
                if (peek(1) == '-') {
                    emitSimple(TokenKind::MinusMinus, 2);
                } else if (peek(1) == '=') {
                    emitSimple(TokenKind::MinusAssign, 2);
                } else {
                    emitSimple(TokenKind::Minus, 1);
                }
                return;
            }
            case '*': {
                if (peek(1) == '*' && peek(2) == '=') {
                    emitSimple(TokenKind::StarStarAssign, 3);
                } else if (peek(1) == '*') {
                    emitSimple(TokenKind::StarStar, 2);
                } else if (peek(1) == '=') {
                    emitSimple(TokenKind::StarAssign, 2);
                } else {
                    emitSimple(TokenKind::Star, 1);
                }
                return;
            }
            case '/': {
                if (peek(1) == '=') {
                    emitSimple(TokenKind::SlashAssign, 2);
                } else {
                    emitSimple(TokenKind::Slash, 1);
                }
                return;
            }
            case '%': {
                if (peek(1) == '=') {
                    emitSimple(TokenKind::PercentAssign, 2);
                } else {
                    emitSimple(TokenKind::Percent, 1);
                }
                return;
            }
            case '&': {
                if (peek(1) == '&' && peek(2) == '=') {
                    emitSimple(TokenKind::AndAndAssign, 3);
                } else if (peek(1) == '&') {
                    emitSimple(TokenKind::AndAnd, 2);
                } else if (peek(1) == '=') {
                    emitSimple(TokenKind::AmpersandAssign, 2);
                } else {
                    emitSimple(TokenKind::Ampersand, 1);
                }
                return;
            }
            case '|': {
                if (peek(1) == '|' && peek(2) == '=') {
                    emitSimple(TokenKind::OrOrAssign, 3);
                } else if (peek(1) == '|') {
                    emitSimple(TokenKind::OrOr, 2);
                } else if (peek(1) == '=') {
                    emitSimple(TokenKind::PipeAssign, 2);
                } else {
                    emitSimple(TokenKind::Pipe, 1);
                }
                return;
            }
            case '^': {
                if (peek(1) == '=') {
                    emitSimple(TokenKind::CaretAssign, 2);
                } else {
                    emitSimple(TokenKind::Caret, 1);
                }
                return;
            }
            case '\\': {
                ++i_;
                errorAt(diagcode::kInvalidCharacter, start,
                        static_cast<std::uint32_t>(i_),
                        "unexpected '\\' outside a string or template literal");
                Token t;
                t.kind = TokenKind::Backslash;
                t.span = span(start, static_cast<std::uint32_t>(i_));
                t.lexeme = file_.slice(t.span);
                commit(std::move(t));
                return;
            }
            default:
                break;
        }

        // Any other byte is an invalid character; consume one byte and recover.
        ++i_;
        errorAt(diagcode::kInvalidCharacter, start, static_cast<std::uint32_t>(i_),
                std::string("unexpected character '") + c + "'");
        Token t;
        t.kind = TokenKind::Invalid;
        t.span = span(start, static_cast<std::uint32_t>(i_));
        t.lexeme = file_.slice(t.span);
        commit(std::move(t));
    }

    // -- state -----------------------------------------------------------------
    const SourceFile& file_;
    DiagnosticSink& sink_;
    std::string_view text_;
    std::size_t n_;
    std::size_t i_ = 0;

    std::vector<Token> tokens_;
    std::vector<int> templateDepth_;  // brace depth per open `${ ... }`
    TokenKind prev_ = TokenKind::Unknown;
    bool hasPrev_ = false;
    bool pendingNewline_ = false;
};

}  // namespace

TokenStream Lexer::lex() { return Scanner(file_, sink_).run(); }

}  // namespace cp3
