// =============================================================================
// src/cli/src/detail/native_packages.cpp - std-module native backends DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/native_packages.hpp.
//
// Aggregates the per-family rows (src/std/<family>/native.hpp) into the
// selection table the build driver consults. Adding a backend for a new module
// means adding its row header + one include + one row pointer here; no other
// file in the cli module changes.
// =============================================================================
#include "detail/native/native_packages.hpp"

#include <algorithm>

#include "detail/native/native_regex.hpp"

#include "../../../../std/buffer/native.hpp"
#include "../../../../std/collections/native.hpp"
#include "../../../../std/console/native.hpp"
#include "../../../../std/crypto/native.hpp"
#include "../../../../std/date/native.hpp"
#include "../../../../std/env/native.hpp"
#include "../../../../std/fs/native.hpp"
#include "../../../../std/json/native.hpp"
#include "../../../../std/math/native.hpp"
#include "../../../../std/network/codegen/native.hpp"
#include "../../../../std/os/native.hpp"
#include "../../../../std/path/native.hpp"
#include "../../../../std/process/native.hpp"
#include "../../../../std/string/native.hpp"
#include "../../../../std/typed-arrays/native.hpp"

namespace cp3::cli::detail {

const std::vector<const NativePackage*>& nativePackages() {
    static const std::vector<const NativePackage*> rows = {
        &kCollectionsNativePackage,
        &kConsoleNativePackage,
        &kFsNativePackage,
        &kJsonNativePackage,
        &kMathNativePackage,
        &kOsNativePackage,
        &kPathNativePackage,
        &kProcessNativePackage,
        &kStringNativePackage,
        &kCryptoNativePackage,
        &kNetworkNativePackage,
        &kTypedArraysNativePackage,
        &kDateNativePackage,
        &kBufferNativePackage,
        &kEnvNativePackage,
    };
    return rows;
}

std::vector<const NativePackage*> selectNativePackages(
    const std::vector<std::string>& importedStdSpecifiers) {
    std::vector<const NativePackage*> selected;
    for (const NativePackage* row : nativePackages()) {
        const bool imported =
            std::find(importedStdSpecifiers.begin(), importedStdSpecifiers.end(),
                      row->specifier) != importedStdSpecifiers.end();
        if (imported) selected.push_back(row);
    }
    return selected;
}

std::vector<const NativePackage*> selectCoreNativePackages(bool usesRegex) {
    std::vector<const NativePackage*> selected;
    if (usesRegex) selected.push_back(&kRegexCoreNativePackage);
    return selected;
}

}  // namespace cp3::cli::detail
