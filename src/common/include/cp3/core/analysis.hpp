// =============================================================================
// cp3/analysis.hpp - the sema side-table contract (DECISIONS D1, D6, D14).
//
// FROZEN INTERFACE (Beta One, frozen by the contracts pass).
//
// Sema does NOT mutate the AST (see ast.hpp design rules). It publishes every
// result it computes into a `SemaResult`, keyed by `Node::nodeId`:
//   * nodeTypes  - inferred type of an expression / declared type of a binding;
//   * symbols    - the binding table (one entry per declaration site);
//   * functions  - resolved signature of every function-like declaration;
//   * docs       - the doc comment of a declaration (one entry per documented
//                 declaration site).
//
// nodeId contract (D6): ids are 1-based, assigned by the parser in source
// (pre-order) order; `0` means "unassigned". The Program root is numbered too.
// A lookup that misses returns nullptr / a default - never throws.
//
// DELIBERATELY DEFERRED side tables (do not half-implement - leave the room):
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

#include "cp3/core/source.hpp"
#include "cp3/core/types.hpp"

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

// One documented parameter: the name as written in the doc comment and the
// text that describes it.
struct DocParam {
    std::string name;
    std::string text;
};

// The parsed documentation of one declaration: the free-text description of a
// JSDoc-style doc comment plus its `@param` / `@returns` tags.
//
// A doc comment is pure documentation: it never changes what a program does,
// and a declaration without one simply has no entry in the table below.
struct DocComment {
    std::string summary;           // the free-text description
    std::vector<DocParam> params;  // the `@param` tags, in source order
    std::string returns;            // the `@returns` / `@return` tag

    bool empty() const noexcept {
        return summary.empty() && params.empty() && returns.empty();
    }
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
    // Resolved declaration node for each identifier use. Consumers such as
    // escape analysis must distinguish shadowed names with the same spelling.
    std::unordered_map<std::uint32_t, std::uint32_t> resolvedBindings;
    std::unordered_map<std::uint32_t, SymbolInfo> symbols;
    std::unordered_map<std::uint32_t, FunctionInfo> functions;

    // The documentation attached to a declaration, keyed by the declaration's
    // nodeId - the same key the symbol table uses, so an editor can look the
    // documentation up by symbol. A declaration without a doc comment has no
    // entry (the table is empty for a program that documents nothing).
    std::unordered_map<std::uint32_t, DocComment> docs;

    // True when sema completed with no error/fatal diagnostic (matches the
    // bool returned by SemanticAnalyzer::analyze).
    bool ok = false;

    // D79: the unit calls `createServer({ threads })` with a thread count that
    // is not the literal 1, so codegen must make module-level bindings
    // thread-local and replay their runtime initializers on every loop thread.
    // False for every unit without the option, which keeps today's lowering.
    bool networkScaleMode = false;

    // Stage 1 of automatic cycle handling: class names whose field types can
    // reach themselves through class-typed fields (arrays, Map/Set values,
    // unions and closures included; unknown edges count as cycle-capable).
    std::vector<std::string> cycleCapableClasses;

    // Shared store/escape-site pass (automatic cycle handling): one string
    // per strong field whose type references a cycle-capable class, e.g.
    // "Node.next : Node | null (weak: no)". Consumed by `cp3 check --report`
    // together with cycleCapableClasses to compute the manual-weak share.
    std::vector<std::string> cycleEscapeSites;

    // The inferred type of `nodeId`, or nullptr when there is none.
    const Type* typeOf(std::uint32_t nodeId) const {
        const auto it = nodeTypes.find(nodeId);
        return it == nodeTypes.end() ? nullptr : &it->second;
    }

    // Resets every table to empty and `ok` to false. Used when a SemaResult is
    // reused across units.
    void clear() {
        nodeTypes.clear();
        resolvedBindings.clear();
        symbols.clear();
        functions.clear();
        docs.clear();
        ok = false;
        networkScaleMode = false;
    }
};

}  // namespace cp3
