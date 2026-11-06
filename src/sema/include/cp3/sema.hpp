// =============================================================================
// cp3/sema.hpp - AST -> diagnostics / resolved semantics.
//
// OWNERSHIP: sema module. The current implementation is a STUB: analyze() does
// nothing and reports no diagnostics. Replace the body; the public shape is the
// frozen boundary (extended and frozen by the contracts pass - DECISIONS D14).
//
// Sema intentionally stores its results in side tables keyed by Node::nodeId;
// it does NOT mutate the AST (see ast.hpp design rules). Those tables live in
// `SemaResult` (cp3/analysis.hpp) and are keyed by the 1-based nodeId contract
// of DECISIONS D6 (0 means "unassigned").
// =============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "cp3/core/analysis.hpp"
#include "cp3/core/ast.hpp"
#include "cp3/diag/diagnostic.hpp"
#include "cp3/host/options.hpp"

namespace cp3 {

// What one imported binding means in the importing unit. The module loader
// already validates that the export exists; sema needs the exported value's
// type (and a function's full signature) so a use is checked and lowered as
// the real thing rather than as an untyped ambient name.
struct ImportedBinding {
    Type type;                       // value type (or nominal type when isType)
    bool isValue = true;             // a value binding exists at the use site
    bool isType = false;             // usable in a type position
    bool isFunction = false;
    // The declared type parameters of a generic function export (empty for
    // ordinary functions), in declaration order.
    std::vector<Type> typeParams;
    std::vector<Type> paramTypes;
    std::vector<bool> paramOptional;
    bool hasRest = false;
    Type returnType;
    bool isAsync = false;
};

using ImportedBindings = std::unordered_map<std::string, ImportedBinding>;

// Name resolution + type checking for one Program. Reports problems through
// the sink. Returns true when no error/fatal diagnostic was reported.
class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(DiagnosticSink& sink) noexcept : sink_(sink) {}

    // The project-aware form (spec 03): `options.typeSystem` selects the plain
    // type-name vocabulary and `options.strictNumericCasts` enables Rust-style
    // refusal of implicit native-width conversions. Additive: the one-argument
    // constructor keeps the TypeScript defaults.
    //
    // `imports` carries the exported types of this unit's resolved module
    // dependencies, keyed by the local binding name. It must outlive the
    // analyze() call; nullptr keeps the historical permissive ambient import
    // behavior (used for std/native/unresolved specifiers).
    SemanticAnalyzer(DiagnosticSink& sink, const CompileOptions& options,
                     const ImportedBindings* imports = nullptr) noexcept
        : sink_(sink), options_(options), imports_(imports) {}

    // Analyzes `program` and publishes every inferred type / symbol / signature
    // into `out` (keyed by Node::nodeId). `out` is cleared first. Returns
    // `out.ok` - true when no error/fatal diagnostic was reported.
    bool analyze(Program& program, SemaResult& out);

    // Convenience: analyze and discard the side tables when the caller only
    // needs the yes/no answer (e.g. `cp3 check`).
    bool analyze(Program& program);

private:
    DiagnosticSink& sink_;
    CompileOptions options_{};
    const ImportedBindings* imports_ = nullptr;
};

}  // namespace cp3
