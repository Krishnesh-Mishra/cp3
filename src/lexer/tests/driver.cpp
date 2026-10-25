// =============================================================================
// src/lexer/tests/driver.cpp - standalone lexer verification driver.
//
// Not part of the CMake build (see the lexer owner's report). Compile directly:
//
//   clang++ -std=c++20 -I src/common/include -I src/lexer/include <sources>
//       -o <tmp>
//   (sources: this driver, src/lexer/src/lexer.cpp, src/common/src/*.cpp)
//
// Prints every token (kind, span, payload) and every diagnostic for a set of
// representative snippets, or for a file given as argv[1].
//
// It also runs a focused assertion pass at the end (verifyGap()) that checks the
// exact token-kind sequence for `>>>=` and its neighbours; the process exits
// nonzero if any of those checks fails.
// =============================================================================
#include "cp3/lexer.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace cp3;

namespace {

std::string escapeForPrint(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\0': out += "\\0"; break;
            case '\\': out += "\\\\"; break;
            default: out.push_back(static_cast<char>(c)); break;
        }
    }
    return out;
}

void runCase(const std::string& name, const std::string& src) {
    std::printf("==== %s ====\n", name.c_str());
    SourceManager sm;
    SourceFile* file = sm.addFile(name + ".cp3", src);
    CollectingDiagnosticSink sink;
    Lexer lexer(*file, sink);
    TokenStream stream = lexer.lex();
    const auto& toks = stream.tokens();

    for (std::size_t i = 0; i < toks.size(); ++i) {
        const Token& t = toks[i];
        std::printf("%3d %-20s %u:%u-%u:%u off[%u,%u) nl=%d",
                    static_cast<int>(i), tokenKindName(t.kind),
                    t.span.begin.line, t.span.begin.column,
                    t.span.end.line, t.span.end.column,
                    t.span.begin.offset, t.span.end.offset,
                    t.precededByNewline ? 1 : 0);
        if (t.kind == TokenKind::NumericLiteral) {
            std::printf("  num=%g", t.numberValue);
        }
        if (t.kind == TokenKind::StringLiteral || t.kind == TokenKind::TemplateString ||
            t.kind == TokenKind::TemplateHead || t.kind == TokenKind::TemplateMiddle ||
            t.kind == TokenKind::TemplateTail) {
            std::printf("  str=[%s] raw=[%s]", escapeForPrint(t.stringValue).c_str(),
                        escapeForPrint(t.rawText).c_str());
            if (t.unterminated) std::printf(" UNTERMINATED");
        }
        if (t.kind == TokenKind::RegExpLiteral) {
            std::printf("  pat=[%s] flags=[%s]", t.stringValue.c_str(), t.rawText.c_str());
        }
        std::printf("  lexeme=[%s]\n", escapeForPrint(std::string(t.lexeme)).c_str());
    }
    for (const auto& d : sink.diagnostics()) {
        std::printf("DIAG  %s  %u:%u  %s\n", d.code.c_str(), d.span.begin.line,
                    d.span.begin.column, d.message.c_str());
    }
    std::printf("\n");
}

// -----------------------------------------------------------------------------
// Focused assertion checks (added for DECISIONS D53): prove the exact token-kind
// sequence produced for `>>>=` and that the neighbouring `>>>`, `>>=`, `>=`,
// `>>` operators are unchanged. Returns the number of failed checks; main()
// exits nonzero when any check fails.
// -----------------------------------------------------------------------------
std::string joinKinds(const std::vector<TokenKind>& kinds) {
    std::string s;
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        if (i != 0) s += ", ";
        s += tokenKindName(kinds[i]);
    }
    return s;
}

// C++ enum spelling for the kinds exercised by the gap check (falls back to
// tokenKindName for anything else). Lets the proof read as the required
// `Identifier, UnsignedShiftRightAssign, Identifier, EndOfFile`.
std::string joinEnumNames(const std::vector<TokenKind>& kinds) {
    std::string s;
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        if (i != 0) s += ", ";
        switch (kinds[i]) {
            case TokenKind::UnsignedShiftRightAssign: s += "UnsignedShiftRightAssign"; break;
            case TokenKind::UnsignedShiftRight: s += "UnsignedShiftRight"; break;
            case TokenKind::ShiftRightAssign: s += "ShiftRightAssign"; break;
            case TokenKind::ShiftRight: s += "ShiftRight"; break;
            case TokenKind::GreaterEqual: s += "GreaterEqual"; break;
            default: s += tokenKindName(kinds[i]); break;
        }
    }
    return s;
}

int checkSequence(const char* src, const std::vector<TokenKind>& expected) {
    SourceManager sm;
    SourceFile* file = sm.addFile("<seq>.cp3", src);
    CollectingDiagnosticSink sink;
    Lexer lexer(*file, sink);
    TokenStream stream = lexer.lex();

    std::vector<TokenKind> actual;
    for (const Token& t : stream.tokens()) actual.push_back(t.kind);

    std::printf("%-10s -> %s\n", src, joinEnumNames(actual).c_str());
    if (!actual.empty()) {
        std::printf("           (tokenKindName: %s)\n", joinKinds(actual).c_str());
    }
    if (actual == expected && sink.diagnostics().empty()) {
        std::printf("           PASS\n");
        return 0;
    }
    std::printf("           FAIL: expected %s\n", joinEnumNames(expected).c_str());
    for (const auto& d : sink.diagnostics()) {
        std::printf("           DIAG %s  %s\n", d.code.c_str(), d.message.c_str());
    }
    return 1;
}

int verifyGap() {
    std::printf("==== >>>= token-set gap check (DECISIONS D53) ====\n");
    int failures = 0;
    // The new four-byte operator.
    failures += checkSequence("a >>>= b",
                              {TokenKind::Identifier, TokenKind::UnsignedShiftRightAssign,
                               TokenKind::Identifier, TokenKind::EndOfFile});
    // Neighbouring operators must be untouched.
    failures += checkSequence("a >>> b",
                              {TokenKind::Identifier, TokenKind::UnsignedShiftRight,
                               TokenKind::Identifier, TokenKind::EndOfFile});
    failures += checkSequence("a >>= b",
                              {TokenKind::Identifier, TokenKind::ShiftRightAssign,
                               TokenKind::Identifier, TokenKind::EndOfFile});
    failures += checkSequence("a >= b",
                              {TokenKind::Identifier, TokenKind::GreaterEqual,
                               TokenKind::Identifier, TokenKind::EndOfFile});
    failures += checkSequence("a >> b",
                              {TokenKind::Identifier, TokenKind::ShiftRight,
                               TokenKind::Identifier, TokenKind::EndOfFile});
    std::printf("==== %d failure(s) ====\n\n", failures);
    return failures;
}

// Kept out of `main` to keep it within the function-length budget; the literal
// is too long to sit on one line.
const char* const kCommentsNewlines =
    "let a = 1; // line\n/* block\n comment */ let b = 2;\n/* unterminated\n"
    "let c = 3;\n";

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        std::ifstream in(argv[1], std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "cannot open %s\n", argv[1]);
            return 1;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        runCase(argv[1], ss.str());
        return 0;
    }

    runCase("overview", R"CP3(import console from "std/console";
let a = 1; let b = 2;
console.log(`${a}-${b}`);
)CP3");

    runCase("numbers", R"CP3(0 123 1_000_000 3.14 .5 1. 1e10 1.5e-3 0x1F 0b101 0o17
0xFF_FF 08 1__0 0x 123abc 1_ 0b12
)CP3");

    runCase("strings", R"CP3("hello\n\t\"x\" \x41 \u0041 \u{1F600}" 'single \'q\''
"\q" "\xZZ" "\u12" "\u{110000}"
)CP3");

    runCase("templates", R"CP3(`a${b}c${d}e`  `no sub`  `x${ `inner ${y}` }z`
`empty ${}` `\u0041` `raw\n`
)CP3");

    runCase("regex", R"CP3(let re = /ab+c/gi;
let d = a / b;
let e = (a) / 2;
x /= 3;
let cls = /[/]/;
let bad = /abc/gg;
let u = /unterminated
)CP3");

    runCase("comments-newlines", kCommentsNewlines);

    runCase("operators", R"CP3(a >>>= 1; a >>> b; a >>= 1; a === b; a !== b; a **= 2;
a ??= b; a?.b; a ?? b; f(...args); 0..n; x => x; a?.5:b;
)CP3");

    runCase("keywords", R"CP3(let var const function return if else any delete yield
type interface class async await weak ref from as of get set
)CP3");

    runCase("unterminated", "\"abc\nlet x = 1;\n`open ${ y\n");

    runCase("invalid-chars", "a \\ b # #ok\n");

    runCase("escapes2", R"CP3("\01" "\u{}" "\u{0}" "\u{10FFFF}" "\u{110000}"
)CP3");

    runCase("comment-inline", "let a=1;/*c*/let b=2;\n");

    return verifyGap() == 0 ? 0 : 1;
}
