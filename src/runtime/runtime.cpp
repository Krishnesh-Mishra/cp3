// =============================================================================
// cp3/runtime.cpp — minimal runtime support surface (see cp3/runtime.hpp).
// =============================================================================
#include "cp3/runtime.hpp"

namespace cp3::runtime {

const char* name() noexcept {
    return "cp3-runtime";
}

std::string version() {
    return "0.1.0";
}

}  // namespace cp3::runtime
