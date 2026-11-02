// =============================================================================
// cp3/modules.hpp - module discovery, prelude injection and import resolution.
//
// OWNERSHIP: modules module (src/modules/**). This header is the module's
// public boundary (STYLE.md §3).
//
// `ModuleLoader` turns an entry `SourceFile` that is already registered with a
// `SourceManager` into a `ModuleGraph` (cp3/module.hpp):
//   * §18 prelude discovery: `_prelude.same.cp3` (folder only), `_prelude.cp3`
//     (recursive), `_prelude.weak.cp3` (recursive, stopped by any nested
//     prelude "wall"), injected in the order Same -> Strong -> Weak;
//   * token-level injection: each applicable prelude is lexed once and its
//     tokens are concatenated in front of the unit's own tokens, so a single
//     `Parser` pass assigns the whole assembled program unique nodeIds while
//     every span still points at the file the text came from;
//   * D4 specifier classification and local path resolution;
//   * export-surface validation for every named/default import (CP3304 /
//     CP3604) and CP3601 for modules that cannot be found (unknown std
//     module, missing local file).
//
// The loader does not link sema/codegen: it only lexes, parses and resolves.
// =============================================================================
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "cp3/diag/diagnostic.hpp"
#include "cp3/meta/module.hpp"
#include "cp3/core/source.hpp"

namespace cp3 {

// Resolves a package specifier (`import { x } from "addpkg"`) to the package
// entry `.cp3` file. The CLI supplies this because the package manager owns
// package.json and `.cp3/lock.json`; the module loader only consumes the
// resolved path. Returning nullopt leaves the package edge unresolved and
// reports the normal module-not-found diagnostic.
using PackageEntryResolver =
    std::function<std::optional<std::filesystem::path>(
        const std::string& specifier,
        const std::filesystem::path& projectRoot)>;

// Loads an entry file plus every local/package module it reaches, transitively.
//
// `entry` must already live in `sm` (the CLI adds it before calling). Local
// dependency sources are added to `sm` as they are discovered; `sm` must
// outlive the returned graph, because token lexemes and AST spans view its
// `SourceFile::text`. The returned graph is never null and always carries an
// entry unit whose `ast` is non-null (the parser recovers from syntax errors).
class ModuleLoader {
public:
    explicit ModuleLoader(DiagnosticSink& sink) noexcept : sink_(sink) {}
    ModuleLoader(DiagnosticSink& sink, PackageEntryResolver packageResolver)
        : sink_(sink), packageResolver_(std::move(packageResolver)) {}

    std::unique_ptr<ModuleGraph> load(SourceManager& sm, const SourceFile& entry);

private:
    DiagnosticSink& sink_;
    PackageEntryResolver packageResolver_;
};

}  // namespace cp3
