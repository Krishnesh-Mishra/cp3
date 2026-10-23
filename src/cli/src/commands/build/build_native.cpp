// =============================================================================
// src/cli/src/commands/build_native.cpp - the native binding build integration.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/native_build.hpp.
//
// A native binding records what its C++ library needs: the include roots, the
// preprocessor defines, the library directories, the libraries to link and the
// runtime DLLs the executable must find next to it. This unit adds those to
// the build (the incremental profile and `--release` alike) and copies the
// runtime DLLs.
//
// The recorded paths are absolute (native-install wrote them that way) or
// relative to the project root, which the module resolver resolved when it
// loaded the binding.
// =============================================================================
#include "detail/native/native_build.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "detail/pkg/package_json.hpp"

namespace cp3::cli::detail {

namespace fs = std::filesystem;

namespace {

// Copies one recorded runtime library next to the executable.
bool copyRuntimeDll(const std::string& id, const std::string& recorded,
                    const fs::path& targetDir) {
    std::error_code ec;
    const fs::path source(recorded);
    if (!fs::is_regular_file(source, ec)) {
        std::fprintf(stderr,
                     "cp3c: the native binding '%s' records the runtime library "
                     "'%s', which does not exist\n",
                     id.c_str(), recorded.c_str());
        return false;
    }
    fs::create_directories(targetDir, ec);
    fs::copy_file(source, targetDir / source.filename(),
                  fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::fprintf(stderr,
                     "cp3c: cannot copy the native runtime library '%s' next to "
                     "the executable: %s\n",
                     recorded.c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

// The runtime DLLs of every binding, next to the executable.
bool copyRuntimeDlls(const std::vector<NativeBinding>& bindings,
                     const BuildLayout& layout) {
    if (bindings.empty()) return true;
    // package.json's "build": { "bundleNativeDlls": false } turns the copying
    // off (the DLLs are then the user's to provide).
    if (!bundleNativeDlls(layout.projectRoot)) return true;
    const fs::path targetDir = layout.exePath.parent_path();
    for (const NativeBinding& binding : bindings) {
        for (const std::string& dll : binding.module.runtimeDlls) {
            if (!copyRuntimeDll(binding.id, dll, targetDir)) return false;
        }
    }
    return true;
}

}  // namespace

bool applyNativeBindings(const std::vector<NativeBinding>& bindings,
                         BuildLayout& layout, NativeBuildFlags& flags,
                         bool msvc) {
    if (bindings.empty()) return true;
    std::error_code ec;
    for (const NativeBinding& binding : bindings) {
        for (const std::string& dir : binding.module.includeDirs) {
            const fs::path path(dir);
            if (!fs::is_directory(path, ec)) {
                std::fprintf(stderr,
                             "cp3c: the native binding '%s' records the include "
                             "directory '%s', which does not exist\n",
                             binding.id.c_str(), dir.c_str());
                return false;
            }
            // The include root joins the program's own: the header
            // fingerprint then covers the SDK's headers, so a changed one
            // retires the object (detail/cache.hpp).
            layout.includeDirs.push_back(path);
        }
        for (const std::string& define : binding.module.defines) {
            flags.compile.push_back("-D" + define);
        }
        for (const std::string& dir : binding.module.libDirs) {
            flags.link.push_back(msvc ? "/LIBPATH:" + dir : "-L" + dir);
        }
        for (const std::string& name : binding.module.linkNames) {
            flags.link.push_back(msvc ? name + ".lib" : "-l" + name);
        }
    }
    return copyRuntimeDlls(bindings, layout);
}

}  // namespace cp3::cli::detail
