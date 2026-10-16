// =============================================================================
// src/sema/src/symbols.cpp — publishing into the SemaResult side tables.
//
// Owns every write into `SemaResult` (cp3/analysis.hpp): `nodeTypes` via
// `setType` / `withType`, `SymbolInfo` via `publishSymbol`, `FunctionInfo` via
// `publishFunction`, and the inferred-return update in
// `updateFunctionReturnType`. Centralising the writes keeps the rest of the
// analyzer free of side-table bookkeeping (STYLE.md §8).
// =============================================================================
#include "analyzer.h"

#include <cstdint>
#include <string>
#include <utility>

namespace cp3 {

void Analyzer::setType(const Node& n, const Type& t) {
    if (n.nodeId != 0) out_.nodeTypes[n.nodeId] = t;
}

Type Analyzer::withType(const Node& n, const Type& t) {
    setType(n, t);
    return t;
}

void Analyzer::publishSymbol(const Binding& b) {
    SymbolInfo si;
    si.name = b.name;
    si.type = b.type;
    si.isConst = b.isConst;
    si.isFunction = b.isFunction;
    si.declNodeId = b.declNodeId;
    si.span = b.span;
    out_.symbols[b.declNodeId] = std::move(si);
}

void Analyzer::publishFunction(const FuncSig& sig, const std::string& name) {
    FunctionInfo fi;
    fi.name = name;
    fi.paramTypes = sig.params;
    fi.paramOptional = sig.optional;
    fi.hasRest = sig.rest;
    fi.returnType = sig.ret;
    fi.declNodeId = sig.declNodeId;
    out_.functions[sig.declNodeId] = std::move(fi);
}

void Analyzer::updateFunctionReturnType(std::uint32_t nodeId, const Type& ret) {
    if (nodeId == 0) return;
    auto fit = out_.functions.find(nodeId);
    if (fit != out_.functions.end()) fit->second.returnType = ret;
}

}  // namespace cp3
