// =============================================================================
// cp3/analysis.hpp — the sema side-table contract (DECISIONS D1, D6, D14).
//
// FROZEN INTERFACE (Beta One, frozen by the contracts pass).
//
// Sema does NOT mutate the AST (see ast.hpp design rules). It publishes every
// result it computes into a `SemaResult`, keyed by `Node::nodeId`:
//   * nodeTypes  — inferred type of an expression / declared type of a binding;
//   * symbols    — the binding table (one entry per declaration site);
//   * functions  — resolved signature of every function-like declaration.
//
// nodeId contract (D6): ids are 1-based, assigned by the parser in source
// (pre-order) order; `0` means "unassigned". The Program root is numbered too.
// A lookup that misses returns nullptr / a default — never throws.
//
// DELIBERATELY DEFERRED side tables (do not half-implement — leave the room):
//   * classInfo  : resolved class shapes, base types, member tables (M2+).
//   * enumInfo   : enum member values / underlying type (M2+).
//   * genericInst: monomorphisation / type-argument bindings (M2+).
//   * imports    : per-unit import bindings (module resolution lives in
//                  cp3/module.hpp; sema will add a table here when needed).
// When one of these lands it is added as a new member of `SemaResult`, so the
// existing members above never have to change shape.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "cp3/source.hpp"
#include "cp3/types.hpp"

namespace cp3 {

// One resolved binding: a `let`/`const`/`var`/parameter, an imported name, or a
// function name. `declNodeId` points at the declaring node so consumers can
// recover the span (and vice versa).
struct SymbolInfo {
    std::string name;
    Type type;
    bool isConst = false;
    bool isFunction = false;
    std::uint32_t declNodeId = 0;
    SourceSpan span{};
};

// The resolved signature of a function-like declaration (function, arrow,
// method). `returnType` is `void` for a body that yields nothing.
struct FunctionInfo {
    std::string name;
    std::vector<Type> paramTypes;
    std::vector<bool> paramOptional;  // parallel to paramTypes
    bool hasRest = false;             // trailing `...rest`
    Type returnType;
    std::uint32_t declNodeId = 0;
};

// Everything sema learned about one Program, keyed by nodeId (D6).
struct SemaResult {
    std::unordered_map<std::uint32_t, Type> nodeTypes;
    std::unordered_map<std::uint32_t, SymbolInfo> symbols;
    std::unordered_map<std::uint32_t, FunctionInfo> functions;

    // True when sema completed with no error/fatal diagnostic (matches the
    // bool returned by SemanticAnalyzer::analyze).
    bool ok = false;

    // The inferred type of `nodeId`, or nullptr when there is none.
    const Type* typeOf(std::uint32_t nodeId) const {
        const auto it = nodeTypes.find(nodeId);
        return it == nodeTypes.end() ? nullptr : &it->second;
    }

    // Resets every table to empty and `ok` to false. Used when a SemaResult is
    // reused across units.
    void clear() {
        nodeTypes.clear();
        symbols.clear();
        functions.clear();
        ok = false;
    }
};

}  // namespace cp3
