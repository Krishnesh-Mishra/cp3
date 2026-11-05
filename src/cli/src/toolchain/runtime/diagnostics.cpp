// =============================================================================
// src/cli/src/toolchain/diagnostics.cpp - diagnostic rendering DEFINITIONS (DECISIONS D2).
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/diagnostics.hpp.
//
// The machine line format is the D2 contract matched by
// tests/runner/run_tests.py and must never change:
//   <path>:<line>:<col>: <severity>: <CODE>: <message>
// The pretty block is purely additive.
// =============================================================================
#include "detail/core/diagnostics.hpp"
#include "detail/core/docs.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace cp3::cli {

namespace {

const char* severityWord(Severity s) noexcept {
    switch (s) {
        case Severity::Note:
            return "note";
        case Severity::Warning:
            return "warning";
        case Severity::Error:
        case Severity::Fatal:
            return "error";
    }
    return "error";
}

const char* sevColor(Severity s, bool color) noexcept {
    if (!color) return "";
    switch (s) {
        case Severity::Error:
        case Severity::Fatal:
            return "\x1b[31m";  // red
        case Severity::Warning:
            return "\x1b[33m";  // yellow
        case Severity::Note:
            return "\x1b[36m";  // cyan
    }
    return "";
}

const char* reset(bool color) noexcept { return color ? "\x1b[0m" : ""; }

const char* bold(bool color) noexcept { return color ? "\x1b[1m" : ""; }

// The exact machine line, no trailing newline.
std::string machineLine(const Diagnostic& d, const std::string& fallback) {
    std::string path = d.span.valid() ? d.span.file->path : fallback;
    if (path.empty()) path = "<unknown>";
    std::uint32_t line = d.span.valid() ? d.span.begin.line : 1;
    std::uint32_t col = d.span.valid() ? d.span.begin.column : 1;
    if (line == 0) line = 1;
    if (col == 0) col = 1;
    return path + ":" + std::to_string(line) + ":" + std::to_string(col) + ": " +
           severityWord(d.severity) + ": " + d.code + ": " + d.message;
}

// A source excerpt + caret block, printed under the machine line when pretty.
void renderPretty(const Diagnostic& d, const RenderOptions& opts, std::FILE* out) {
    const bool color = opts.color;
    std::fprintf(out, "%s%s%s[%s]%s: %s\n", bold(color),
                 sevColor(d.severity, color), severityWord(d.severity),
                 d.code.c_str(), reset(color), d.message.c_str());
    if (!d.span.valid() || d.span.file == nullptr) {
        std::fprintf(out, "  (no source location)\n");
        return;
    }
    const std::string& path = d.span.file->path;
    const std::uint32_t line = d.span.begin.line == 0 ? 1 : d.span.begin.line;
    const std::uint32_t col = d.span.begin.column == 0 ? 1 : d.span.begin.column;
    std::fprintf(out, "  --> %s:%u:%u\n", path.c_str(), line, col);

    const std::string_view text = d.span.file->lineText(line);
    const std::string gutter = std::to_string(line);
    const std::string pad(gutter.size(), ' ');

    std::fprintf(out, "  %s |\n", pad.c_str());
    std::fprintf(out, "  %s | %.*s\n", gutter.c_str(),
                 static_cast<int>(text.size()), text.data());
    std::fprintf(out, "  %s | ", pad.c_str());
    // Caret column is a byte column; clamp into the line.
    std::uint32_t caretCol = col == 0 ? 1 : col - 1;
    if (caretCol > text.size()) caretCol = static_cast<std::uint32_t>(text.size());
    for (std::uint32_t i = 0; i < caretCol; ++i) std::fputc(' ', out);
    std::uint32_t span = d.span.length();
    if (span == 0) span = 1;
    if (text.empty()) span = 1;
    for (std::uint32_t i = 0; i < span; ++i) std::fputc('^', out);
    std::fputc('\n', out);
}

}  // namespace

void renderDiagnostics(const std::vector<Diagnostic>& diagnostics,
                       const RenderOptions& opts, std::FILE* out) {
    for (const Diagnostic& d : diagnostics) {
        const std::string machine = machineLine(d, opts.fallbackPath);
        std::fputs(machine.c_str(), out);
        std::fputc('\n', out);
        if (opts.pretty) {
            renderPretty(d, opts, out);
        }
        if (d.severity == Severity::Error || d.severity == Severity::Fatal) {
            const std::string help = writeDiagnosticHelpPage(d);
            if (!help.empty()) {
                std::fprintf(out, "  help: %s\n", help.c_str());
            }
        }
        for (const DiagnosticNote& note : d.notes) {
            Diagnostic synthetic(note.message.empty() ? d.code : d.code,
                                 Severity::Note, note.span, note.message);
            std::string line = machineLine(synthetic, opts.fallbackPath);
            std::fputs(line.c_str(), out);
            std::fputc('\n', out);
            if (opts.pretty && note.span.valid()) {
                renderPretty(synthetic, opts, out);
            }
        }
        if (opts.pretty) {
            // Std import hints (sema stdImportHint, embedded by
            // Analyzer::reportUndefinedName into the message AND offered as
            // a suggestion) must stay visible on the CLI path, not only in
            // the LSP. The machine line above already carries the exact
            // import line inside the message (D2 contract, unchanged), so
            // non-TTY runs get it too; this pretty-only fallback re-surfaces
            // it as `help:` if a diagnostic producer ever sets the message
            // without the matching suggestion. It never adds a machine
            // line and never rewrites the message.
            std::string importHint;
            const std::size_t importAt = d.message.find("import ");
            if (importAt != std::string::npos) {
                const std::size_t endAt = d.message.find(';', importAt);
                if (endAt != std::string::npos) {
                    importHint = d.message.substr(importAt, endAt - importAt + 1);
                }
            }
            bool hintAlreadyShown = false;
            for (const std::string& suggestion : d.suggestions) {
                if (!importHint.empty() &&
                    suggestion.find(importHint) != std::string::npos) {
                    hintAlreadyShown = true;
                }
                std::fprintf(out, "  help: %s\n", suggestion.c_str());
            }
            if (!importHint.empty() && !hintAlreadyShown) {
                std::fprintf(out, "  help: Add `%s`\n", importHint.c_str());
            }
        }
    }
}

}  // namespace cp3::cli
