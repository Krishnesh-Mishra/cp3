// cp3/version.hpp - compiler identity/version. FROZEN INTERFACE.
#pragma once

#include <string>

namespace cp3 {

inline constexpr const char* kCompilerName = "cp3c";
inline constexpr const char* kVersion = "0.1.0";
inline constexpr const char* kVersionStage = "beta.1";

// e.g. "cp3c 0.1.0-beta.1"
std::string versionString();

}  // namespace cp3
