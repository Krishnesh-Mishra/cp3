// =============================================================================
// cp3/diagnostic.hpp - severities, diagnostics, and the DiagnosticSink.
//
// FROZEN INTERFACE (Beta One). See ARCHITECTURE.md and diagnostic_codes.hpp.
//
// Every frontend stage (lexer, parser, sema, codegen) reports problems through
// a DiagnosticSink rather than printing directly. That keeps the compiler
// testable and lets the CLI own all rendering (docs/cli-tooling/02-...).
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cp3/core/source.hpp"

namespace cp3 {

enum class Severity {
    Note,
    Warning,
    Error,
    Fatal,
};

const char* severityName(Severity s) noexcept;

// A secondary location attached to a diagnostic ("first declared here").
struct DiagnosticNote {
    SourceSpan span{};
    std::string message;
};

// One reported problem. `code` is a stable "CP3xxx" string (see
// diagnostic_codes.hpp). `span` may be invalid for whole-program diagnostics.
struct Diagnostic {
    std::string code;                       // e.g. "CP3401"
    Severity severity = Severity::Error;
    SourceSpan span{};                      // primary location
    std::string message;                    // cp3-native, human-readable
    std::vector<DiagnosticNote> notes;      // related locations
    std::vector<std::string> suggestions;   // "Did you mean ...?" entries

    Diagnostic() = default;
    Diagnostic(std::string code, Severity sev, SourceSpan span, std::string message)
        : code(std::move(code)),
          severity(sev),
          span(span),
          message(std::move(message)) {}
};

// The interface every compiler stage reports through.
//
// Subclasses implement doReport(); report()/counters are handled here so
// hasErrors()/errorCount() are consistent for every sink.
class DiagnosticSink {
public:
    virtual ~DiagnosticSink() = default;

    DiagnosticSink(const DiagnosticSink&) = delete;
    DiagnosticSink& operator=(const DiagnosticSink&) = delete;

    void report(Diagnostic d) {
        if (d.severity == Severity::Error || d.severity == Severity::Fatal) {
            ++errorCount_;
        } else if (d.severity == Severity::Warning) {
            ++warningCount_;
        }
        doReport(d);
    }

    bool hasErrors() const noexcept { return errorCount_ > 0; }
    std::uint32_t errorCount() const noexcept { return errorCount_; }
    std::uint32_t warningCount() const noexcept { return warningCount_; }

    // Convenience helpers - keep call sites short.
    void error(std::string code, const SourceSpan& span, std::string message) {
        report(Diagnostic(std::move(code), Severity::Error, span, std::move(message)));
    }
    void warning(std::string code, const SourceSpan& span, std::string message) {
        report(Diagnostic(std::move(code), Severity::Warning, span, std::move(message)));
    }
    void fatal(std::string code, const SourceSpan& span, std::string message) {
        report(Diagnostic(std::move(code), Severity::Fatal, span, std::move(message)));
    }

protected:
    DiagnosticSink() = default;
    virtual void doReport(const Diagnostic& d) = 0;

private:
    std::uint32_t errorCount_ = 0;
    std::uint32_t warningCount_ = 0;
};

// Records every diagnostic in memory (tests, `cp3 check`).
class CollectingDiagnosticSink final : public DiagnosticSink {
public:
    const std::vector<Diagnostic>& diagnostics() const noexcept { return diagnostics_; }
    bool empty() const noexcept { return diagnostics_.empty(); }
    void clear() noexcept { diagnostics_.clear(); }

protected:
    void doReport(const Diagnostic& d) override { diagnostics_.push_back(d); }

private:
    std::vector<Diagnostic> diagnostics_;
};

// Silences everything (used to probe an AST without noise).
class NullDiagnosticSink final : public DiagnosticSink {
protected:
    void doReport(const Diagnostic&) override {}
};

}  // namespace cp3
