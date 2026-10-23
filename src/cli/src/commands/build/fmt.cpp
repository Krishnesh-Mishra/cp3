// =============================================================================
// src/cli/src/commands/fmt.cpp - `cp3c fmt`, a conservative non-destructive formatter.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdFmt.
//
// The formatting rules live in src/format/format.cpp (detail/format.hpp),
// shared with the LSP documentFormatting provider. LIMITATIONS (deliberate,
// documented there): this is NOT the canonical cp3 formatter - that is a later
// job. It never reflows code or rewrites tokens; it only normalizes line
// endings, strips trailing whitespace, ensures a single trailing newline, and
// re-indents code lines by brace depth (2 spaces per level). No non-whitespace
// character is ever changed, so it can never corrupt a file.
// =============================================================================
#include <cstdio>
#include <string>

#include "cp3/diag/diagnostic.hpp"
#include "cp3/lexer.hpp"
#include "cp3/core/source.hpp"

#include "detail/core/commands.hpp"
#include "detail/editor/format.hpp"
#include "detail/core/util.hpp"

namespace cp3::cli {

int cmdFmt(const CliOptions& opt) {
    if (opt.file.empty()) {
        std::fprintf(stderr, "cp3c: fmt needs a .cp3 file\n");
        return kExitUsageError;
    }
    const fs::path path(opt.file);
    std::string text;
    if (!readTextFile(path, text)) {
        std::fprintf(stderr, "cp3c: cannot read '%s'\n", opt.file.c_str());
        return kExitUsageError;
    }

    // Report lexical problems, but never rewrite a file we cannot lex cleanly.
    SourceManager sm;
    SourceFile* file = sm.addFile(opt.file, text);
    CollectingDiagnosticSink sink;
    if (file != nullptr) {
        Lexer lexer(*file, sink);
        (void)lexer.lex();
    }
    renderDiagnostics(sink.diagnostics(), makeRenderOptions(opt, opt.file),
                      stderr);
    if (sink.hasErrors()) {
        std::fprintf(stderr,
                     "cp3c: fmt: refusing to rewrite '%s' (lexical errors)\n",
                     opt.file.c_str());
        return kExitCompileError;
    }

    const std::string formatted = formatDocumentText(text);
    if (formatted == text) {
        if (opt.verbose) {
            std::fprintf(stderr, "cp3c: %s already formatted\n",
                         opt.file.c_str());
        }
        return kExitSuccess;
    }
    if (!writeTextFile(path, formatted, nullptr)) {
        std::fprintf(stderr, "cp3c: cannot write '%s'\n", opt.file.c_str());
        return kExitCompileError;
    }
    std::fprintf(stderr, "cp3c: formatted %s\n", opt.file.c_str());
    return kExitSuccess;
}

}  // namespace cp3::cli
