// cp3/version.hpp - compiler identity/version. FROZEN INTERFACE.
#pragma once

#include <string>

namespace cp3 {

inline constexpr const char* kCompilerName = "cp3c";
inline constexpr const char* kVersion = "0.2.4";
// Appended as "-<stage>" when non-empty. Keep the combined value in sync with
// version.txt at the repository root (vendor/tools/update-version.ps1 does this).
inline constexpr const char* kVersionStage = "";

// e.g. "cp3c 0.2.4"
std::string versionString();

}  // namespace cp3
