// =============================================================================
// cp3/sema.cpp — STUB implementation (see cp3/sema.hpp).
// =============================================================================
#include "cp3/sema.hpp"

namespace cp3 {

bool SemanticAnalyzer::analyze(Program& /*program*/) {
    // STUB: no checks yet. The real analyzer walks the tree, resolves names,
    // assigns types into nodeId-keyed side tables, and reports CP33xx/CP34xx/
    // CP35xx/CP37xx/CP38xx diagnostics through sink_.
    return !sink_.hasErrors();
}

}  // namespace cp3
