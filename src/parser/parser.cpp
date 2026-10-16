// =============================================================================
// cp3/parser.cpp — STUB implementation (see cp3/parser.hpp).
// =============================================================================
#include "cp3/parser.hpp"

#include <memory>

namespace cp3 {

ProgramPtr Parser::parseProgram() {
    // STUB: no grammar yet. Preserve where the (empty) module starts so later
    // stages have a usable span, then hand back an empty Program.
    SourceSpan span{};
    if (!tokens_.atEnd()) {
        span = tokens_.peek().span;
    } else {
        span = TokenStream::eofToken().span;
    }
    return std::make_unique<Program>(std::move(span));
}

}  // namespace cp3
