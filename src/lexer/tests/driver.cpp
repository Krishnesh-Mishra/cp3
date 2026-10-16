// =============================================================================
// src/lexer/tests/driver.cpp — standalone lexer verification driver.
//
// Not part of the CMake build (see the lexer owner's report). Compile directly:
//
//   g++ -std=c++20 -I src/common/include -I src/lexer/include \
//       src/lexer/tests/driver.cpp src/lexer/lexer.cpp src/common/src/*.cpp \
//       -o <tmp>
//
// Prints every token (kind, span, payload) and every diagnostic for a set of
// representative snippets, or for a file given as argv[1].
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
        std::printf("%3zu %-20s %u:%u-%u:%u off[%u,%u) nl=%d",
                    i, tokenKindName(t.kind),
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

    runCase("comments-newlines", "let a = 1; // line\n/* block\n comment */ let b = 2;\n/* unterminated\nlet c = 3;\n");

    runCase("operators", R"CP3(a >>>= 1; a >>> b; a >>= 1; a === b; a !== b; a **= 2;
a ??= b; a?.b; a ?? b; f(...args); 0..n; x => x; a?.5:b;
)CP3");

    runCase("keywords", R"CP3(let var const function return if else any delete yield
type interface class async await weak ref from as of get set
)CP3");

    runCase("unterminated", "\"abc\nlet x = 1;\n`open ${ y\n");

    return 0;
}
