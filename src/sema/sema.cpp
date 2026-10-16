// =============================================================================
// cp3/sema.cpp — STUB implementation (see cp3/sema.hpp).
// =============================================================================
#include "cp3/sema.hpp"

namespace cp3 {

bool SemanticAnalyzer::analyze(Program& /*program*/, SemaResult& out) {
    // STUB: no checks yet. The real analyzer walks the tree, resolves names,
    // fills `out.nodeTypes` / `out.symbols` / `out.functions` keyed by
    // Node::nodeId (D6), and reports CP33xx/CP34xx/CP35xx/CP37xx/CP38xx
    // diagnostics through sink_.
    out.clear();
    out.ok = !sink_.hasErrors();
    return out.ok;
}

bool SemanticAnalyzer::analyze(Program& program) {
    SemaResult discarded;
    return analyze(program, discarded);
}

}  // namespace cp3
