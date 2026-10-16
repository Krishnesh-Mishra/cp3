// =============================================================================
// cp3/codegen.cpp — STUB implementation (see cp3/codegen.hpp).
// =============================================================================
#include "cp3/codegen.hpp"

namespace cp3 {

GeneratedModule CodeGenerator::generate(const Program& /*program*/,
                                        const SemaResult& /*sema*/,
                                        const std::string& /*moduleName*/) {
    // STUB: emit nothing yet. The real generator walks the checked AST, reads
    // inferred types from `sema`, and produces a .hpp/.cpp pair plus a source
    // map back to .cp3 locations.
    return GeneratedModule{};
}

GeneratedModule CodeGenerator::generate(const Program& program,
                                        const std::string& moduleName) {
    return generate(program, SemaResult{}, moduleName);
}

}  // namespace cp3
