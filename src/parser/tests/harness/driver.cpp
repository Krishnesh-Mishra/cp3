// =============================================================================
// src/parser/tests/driver.cpp - standalone parser verification driver.
//
// Not part of the CMake build (tests are compiled directly). Compile with:
//
//   clang++ -std=c++20 -I src/common/include -I src/parser/include
//       src/parser/tests/driver.cpp
//       src/parser/tests/programs.cpp
//       src/parser/tests/test_scanner.cpp
//       src/parser/tests/test_scanner_literals.cpp
//       src/parser/tests/ast_printer.cpp
//       src/parser/tests/ast_printer_expr.cpp
//       src/parser/tests/ast_printer_decl.cpp
//       src/parser/tests/ast_printer_stmt.cpp
//       src/parser/src/*.cpp
//       src/common/src/ast.cpp src/common/src/diagnostic.cpp
//       src/common/src/source.cpp src/common/src/token.cpp
//       -o <tmp>
//
// The lexer module is still a stub (it emits only EndOfFile), so the driver
// does NOT use cp3::Lexer; TestScanner self-lexes. The driver then parses the
// stream with the real cp3::Parser and prints the AST plus every diagnostic.
//
// Usage:
//   driver            -> runs the built-in representative programs
//   driver <file.cp3> -> lexes + parses that one file
// =============================================================================
#include "../printer/ast_printer.h"
#include "programs.h"
#include "../scanner/test_scanner.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic.hpp"
#include "cp3/parser.hpp"

namespace cp3test {
namespace {

bool loadFile(const char* path, std::vector<Case>& cases) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    cases.emplace_back(path, ss.str());
    return true;
}

void runCase(const std::string& name, const std::string& src) {
    std::printf("==== %s ====\n", name.c_str());
    cp3::SourceManager sm;
    cp3::SourceFile* file = sm.addFile(name + ".cp3", src);
    cp3::CollectingDiagnosticSink sink;

    TestScanner scanner(*file, sink);
    cp3::TokenStream stream = scanner.scan();
    cp3::Parser parser(std::move(stream), sink);
    cp3::ProgramPtr program = parser.parseProgram();

    printNode(program.get(), 0);

    for (const auto& d : sink.diagnostics()) {
        std::printf("DIAG %s %u:%u %s\n", d.code.c_str(), d.span.begin.line,
                    d.span.begin.column, d.message.c_str());
    }
    std::printf("\n");
}

}  // namespace
}  // namespace cp3test

int main(int argc, char** argv) {
    std::vector<cp3test::Case> cases;
    if (argc > 1) {
        if (!cp3test::loadFile(argv[1], cases)) return 1;
    } else {
        cp3test::builtinCases(cases);
    }

    for (const auto& [name, src] : cases) cp3test::runCase(name, src);
    return 0;
}
