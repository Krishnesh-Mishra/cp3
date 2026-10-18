// =============================================================================
// src/cli/src/commands/lsp.cpp - `cp3c lsp` command surface (LSP MVP glue).
//
// OWNERSHIP: cli module (src/cli/**) for the command; the protocol server
// itself lives in src/lsp/** (PKG-CONTRACTS sec.2 and sec.5).
//
// The command owns ONE thing: adapting the in-process frontend (the same
// stages `cp3c check` runs, detail/pipeline.hpp) into the transport-neutral
// CheckFn the server calls. In-process reuse was chosen over spawning
// `cp3c check` because it needs no subprocess per keystroke, no re-parsing of
// rendered machine lines, and cannot interleave anything into stdout - the
// stages report through a DiagnosticSink that is never rendered here.
// =============================================================================
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "cp3/analysis.hpp"
#include "cp3/diagnostic.hpp"
#include "cp3/diagnostic_codes.hpp"
#include "cp3/module.hpp"
#include "cp3/source.hpp"

#include "cp3/lsp.hpp"

#include "detail/commands.hpp"
#include "detail/pipeline.hpp"

namespace cp3::cli {
namespace {

// The compiler reports four severities; LSP has error/warning/information.
int lspSeverity(Severity severity) {
    switch (severity) {
        case Severity::Error:
        case Severity::Fatal:
            return 1;
        case Severity::Warning:
            return 2;
        case Severity::Note:
            return 3;
    }
    return 1;
}

// True when the span sits in the document the client asked about. Diagnostics
// in other files (an imported module, an injected prelude) are outside the MVP
// surface, which publishes a document's own diagnostics only.
bool belongsTo(const SourceSpan& span, const SourceFile& file) {
    return span.file == &file || span.file->canonicalPath == file.canonicalPath;
}

std::vector<lsp::RawDiagnostic> collectDiagnostics(
    const SourceFile& file, const std::vector<Diagnostic>& diagnostics) {
    std::vector<lsp::RawDiagnostic> out;
    out.reserve(diagnostics.size());
    for (const Diagnostic& d : diagnostics) {
        // A span-less diagnostic is whole-program: attach it to the document.
        if (d.span.valid() && !belongsTo(d.span, file)) continue;
        lsp::RawDiagnostic raw;
        raw.code = d.code;
        raw.severity = lspSeverity(d.severity);
        raw.message = d.message;
        if (d.span.valid()) {
            raw.line = d.span.begin.line;
            raw.column = d.span.begin.column;
            raw.endLine = d.span.end.line;
            raw.endColumn = d.span.end.column;
        }
        out.push_back(std::move(raw));
    }
    return out;
}

// One compile-check of `text` as `path`: the `cp3c check` stages, but the
// diagnostics are handed back as data instead of being rendered.
std::vector<lsp::RawDiagnostic> checkTextOnce(const std::string& path,
                                              const std::string& text) {
    SourceManager sources;
    SourceFile* file = sources.addFile(path, text);
    if (file == nullptr) return {};
    CollectingDiagnosticSink sink;
    const std::unique_ptr<ModuleGraph> graph = loadModuleGraph(sources, *file, sink);
    SemaResult sema;
    CompilationUnit* entry = graph != nullptr ? graph->entry() : nullptr;
    if (entry != nullptr && entry->ast != nullptr) {
        analyzeProgram(*entry->ast, sema, sink);
    }
    return collectDiagnostics(*file, sink.diagnostics());
}

lsp::RawDiagnostic internalError(const std::string& what) {
    lsp::RawDiagnostic raw;
    raw.code = diagcode::kInternalError;
    raw.severity = 1;
    raw.message = "internal error while checking: " + what;
    return raw;
}

// The LSP entry point must never take the server down with an exception.
std::vector<lsp::RawDiagnostic> checkText(const std::string& path,
                                          const std::string& text) {
    try {
        return checkTextOnce(path, text);
    } catch (const std::exception& error) {
        return {internalError(error.what())};
    } catch (...) {
        return {internalError("unknown exception")};
    }
}

}  // namespace

int cmdLsp(const CliOptions& opt) {
    (void)opt;  // `lsp` has its own argument surface (validated in cli.cpp)
    return lsp::runStdioServer(&checkText);
}

}  // namespace cp3::cli
