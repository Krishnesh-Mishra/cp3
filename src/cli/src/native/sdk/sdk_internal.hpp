// =============================================================================
// src/cli/src/native/sdk_internal.hpp - helpers shared by sdk.cpp/sdk_libs.cpp.
//
// OWNERSHIP: cli module (native-install). PRIVATE header.
// =============================================================================
#pragma once

#include <string>

#include "native/sdk/sdk.hpp"

namespace cp3::cli::native {

// Finds static/shared/runtime library files for every requested link (or for
// libraries whose names match the SDK headers), recording absolute paths and
// link names in `out`. A requested link that cannot be found is an error.
bool detectLibraries(const SdkRequest& request, SdkResolution& out,
                     std::string& error);

}  // namespace cp3::cli::native
