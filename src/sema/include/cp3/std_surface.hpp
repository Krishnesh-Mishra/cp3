// =============================================================================
// cp3/std_surface.hpp - the std-namespace surface sema already implements.
//
// OWNERSHIP: sema module (src/sema/**). Declaration-only public header for
// callers outside sema (the LSP intelligence engine, src/cli). The definitions
// live where the tables do: src/sema/src/checks/std_check.cpp dispatches to the
// per-family units under src/std/<family>/sema.cpp, none of which are part of
// the public boundary.
//
// This is the exact surface the checker itself uses when it validates a
// member access on a std namespace (CP3305 candidates), so an editor that
// offers these names can never offer something the compiler rejects as
// unknown, and never hides something it resolves.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace cp3 {

// True for the std namespaces with a closed member table (Math / path / os /
// crypto / console / JSON / Date / fs / process / env): an unknown member is
// CP3305. False for the permissive ones (network), which keep the ambient
// fallthrough and therefore have no candidate list to surface.
bool isClosedStdNamespace(const std::string& ns);

// The implemented member names of the closed namespace `ns`, in table order;
// empty when `ns` names no closed family.
std::vector<std::string> stdNamespaceMemberNames(const std::string& ns);

}  // namespace cp3
