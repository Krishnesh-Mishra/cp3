// =============================================================================
// src/cli/src/commands/native_install_support.cpp - native-install pipeline.
//
// OWNERSHIP: cli module (src/cli/**). Implements the SDK resolution, libclang
// bind, duplicate check, and metadata emission behind cmdNativeInstall.
// =============================================================================
#include <cctype>
#include <cstdio>
#include <string>
#include <system_error>
#include <vector>

#include "detail/core/commands.hpp"
#include "detail/pkg/json.hpp"
#include "detail/native/native_install.hpp"
#include "detail/pkg/package_json.hpp"
#include "detail/core/util.hpp"
#include "native/bindgen/bindgen.hpp"
#include "native/libclang/libclang.hpp"
#include "native/libclang/metadata.hpp"
#include "native/sdk/sdk.hpp"

namespace cp3::cli {

namespace fs = std::filesystem;
namespace native = cp3::cli::native;

namespace {

std::string sanitizeId(const std::string& input) {
    std::string out;
    for (const char c : input) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '-' || c == '_' || c == '.') {
            out.push_back(c);
        } else if (!out.empty() && out.back() != '-') {
            out.push_back('-');
        }
    }
    while (!out.empty() && (out.back() == '-' || out.back() == '.')) {
        out.pop_back();
    }
    return out;
}

std::string jsonArray(const std::vector<std::string>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out += ", ";
        out += jsonQuote(values[i]);
    }
    out += "]";
    return out;
}

std::size_t countMethods(const NativeModule& module) {
    std::size_t count = 0;
    for (const NativeBoundClass& cls : module.classes) {
        count += cls.methods.size();
    }
    return count;
}

bool readIfExists(const fs::path& path, std::string& out) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return false;
    return readTextFile(path, out);
}

// Headers inside the binding's include roots are recorded relative to the
// root, so the generated `#include` (and the binding) survive a moved SDK.
// Any header outside every root keeps its absolute path.
std::vector<std::string> relativeHeaders(
    const std::vector<std::string>& headers,
    const std::vector<std::string>& includeDirs) {
    std::vector<std::string> out;
    out.reserve(headers.size());
    for (const std::string& header : headers) {
        const fs::path absolute(header);
        std::string best;
        for (const std::string& dir : includeDirs) {
            const fs::path relative = absolute.lexically_relative(fs::path(dir));
            if (relative.empty()) continue;
            const std::string text = relative.generic_string();
            if (text.rfind("..", 0) == 0 || text == ".") continue;
            if (best.empty() || text.size() < best.size()) best = text;
        }
        out.push_back(best.empty() ? header : best);
    }
    return out;
}

void printList(const char* label, const std::vector<std::string>& values) {
    if (values.empty()) return;
    std::fprintf(stderr, "cp3c: %s:\n", label);
    for (const std::string& value : values) {
        std::fprintf(stderr, "  %s\n", value.c_str());
    }
}

struct InstallContext {
    std::string id;
    fs::path target;
    fs::path indexPath;
    fs::path metadataPath;
    bool identical = false;
};

struct PreparedBinding {
    native::BindResult result;
    std::string metadataText;
    std::string libPath;
    std::string libVersion;
};

bool resolveContext(const NativeOptions& options, InstallContext& out) {
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    fs::path projectRoot = cwd;
    findPackageRoot(cwd, projectRoot);

    std::string id = options.name;
    if (id.empty()) {
        JsonValue manifest;
        std::string ignored;
        if (loadPackageJson(projectRoot, manifest, ignored)) {
            id = packageName(manifest, projectRoot);
        }
    }
    if (id.empty() && !options.input.empty()) {
        id = fs::path(options.input).filename().string();
    }
    id = sanitizeId(id);
    if (id.empty()) {
        std::fprintf(stderr, "cp3c: cannot derive a binding id; pass --name <id>\n");
        printNativeInstallUsage(stderr);
        return false;
    }
    const fs::path bindingRoot =
        options.out.empty() ? projectRoot / ".cp3" / "native"
                            : (fs::path(options.out).is_absolute()
                                   ? fs::path(options.out)
                                   : projectRoot / options.out);
    out.id = id;
    out.target = bindingRoot / id;
    out.indexPath = out.target / "index.cp3";
    out.metadataPath = out.target / "native.json";
    return true;
}

bool prepareBinding(const InstallContext& ctx,
                    const native::SdkResolution& sdk,
                    const native::LibClang& lib, PreparedBinding& out) {
    native::BindRequest bind;
    bind.headers = relativeHeaders(sdk.headers, sdk.includeDirs);
    bind.includeDirs = sdk.includeDirs;
    bind.defines = sdk.defines;
    bind.publicRoots = sdk.publicRoots;
    bind.moduleId = ctx.id;
    bind.source = sdk.source;
    bind.clangArgs = sdk.clangArgs;

    out.result = native::bindHeaders(lib, bind);
    if (!out.result.ok) {
        std::fprintf(stderr, "cp3c: native-install failed for '%s':\n%s\n",
                     ctx.id.c_str(), out.result.error.c_str());
        return false;
    }
    if (out.result.module.functions.empty() &&
        out.result.module.classes.empty() && out.result.module.enums.empty()) {
        std::fprintf(stderr,
                     "cp3c: native-install found no supported declarations in "
                     "'%s'.\n",
                     sdk.source.c_str());
        for (const std::string& note : out.result.module.rejected) {
            std::fprintf(stderr, "  rejected %s\n", note.c_str());
        }
        return false;
    }

    NativeModule& module = out.result.module;
    module.includeDirs = sdk.includeDirs;
    module.defines = sdk.defines;
    module.headers = bind.headers;
    module.libDirs = sdk.libDirs;
    module.linkNames = sdk.linkNames;
    module.staticLibraries = sdk.staticLibraries;
    module.sharedLibraries = sdk.sharedLibraries;
    module.runtimeDlls = sdk.runtimeDlls;
    module.abi = native::deriveAbi(module.target);
    out.result.indexText = native::renderIndexModule(module);
    out.metadataText = native::renderMetadata(module, out.result.indexText);
    out.libPath = lib.path;
    out.libVersion = lib.version;
    return true;
}

void reportInstall(const NativeOptions& options, const native::SdkResolution& sdk,
                   const PreparedBinding& prepared, const InstallContext& ctx) {
    const NativeModule& module = prepared.result.module;
    if (options.json) {
        std::printf("{\"ok\": true, \"id\": %s, \"source\": %s, ",
                    jsonQuote(ctx.id).c_str(), jsonQuote(sdk.source).c_str());
        std::printf("\"binding\": %s, \"index\": %s, ",
                    jsonQuote(ctx.target.string()).c_str(),
                    jsonQuote(ctx.indexPath.string()).c_str());
        std::printf("\"metadata\": %s, ",
                    jsonQuote(ctx.metadataPath.string()).c_str());
        std::printf("\"dryRun\": %s, ", options.dryRun ? "true" : "false");
        std::printf("\"counts\": {\"functions\": %zu, \"classes\": %zu, "
                    "\"methods\": %zu, \"enums\": %zu, \"rejected\": %zu}, ",
                    module.functions.size(), module.classes.size(),
                    countMethods(module), module.enums.size(),
                    module.rejected.size());
        std::printf("\"includeDirs\": %s, ",
                    jsonArray(module.includeDirs).c_str());
        std::printf("\"libDirs\": %s, ", jsonArray(module.libDirs).c_str());
        std::printf("\"linkNames\": %s, ", jsonArray(module.linkNames).c_str());
        std::printf("\"runtimeDlls\": %s, ",
                    jsonArray(module.runtimeDlls).c_str());
        std::printf("\"rejected\": %s}\n",
                    jsonArray(module.rejected).c_str());
    } else {
        std::printf("native-install: binding '%s'\n", ctx.id.c_str());
        std::printf("  source: %s\n", sdk.source.c_str());
        std::printf("  binding: %s\n", ctx.target.string().c_str());
        std::printf("  declarations: %zu functions, %zu classes (%zu methods), "
                    "%zu enums\n",
                    module.functions.size(), module.classes.size(),
                    countMethods(module), module.enums.size());
        if (!module.rejected.empty()) {
            std::printf("  rejected: %zu (listed in index.cp3)\n",
                        module.rejected.size());
        }
        if (options.dryRun) {
            std::printf("  dry-run: no files written\n");
        } else if (ctx.identical && !options.force) {
            std::printf("  already up to date\n");
        } else {
            std::printf("  wrote %s\n", ctx.indexPath.string().c_str());
            std::printf("  wrote %s\n", ctx.metadataPath.string().c_str());
        }
    }

    if (!module.rejected.empty()) {
        std::fflush(stdout);
        std::fprintf(stderr, "cp3c: native-install warnings for '%s':\n",
                     ctx.id.c_str());
        for (const std::string& note : module.rejected) {
            std::fprintf(stderr, "  rejected %s\n", note.c_str());
        }
        std::fflush(stderr);
    }
    if (options.verbose) {
        std::fprintf(stderr, "cp3c: libclang: %s\n", prepared.libPath.c_str());
        std::fprintf(stderr, "cp3c: libclang version: %s\n",
                     prepared.libVersion.c_str());
        std::fprintf(stderr, "cp3c: target: %s\n", module.target.c_str());
        printList("headers", module.headers);
    }
}

bool checkOverwrite(const NativeOptions& options, InstallContext& ctx,
                    const PreparedBinding& prepared) {
    std::string existingIndex;
    std::string existingMetadata;
    const bool hasIndex = readIfExists(ctx.indexPath, existingIndex);
    const bool hasMetadata = readIfExists(ctx.metadataPath, existingMetadata);
    std::error_code ec;
    const bool exists = hasIndex || hasMetadata || fs::exists(ctx.target, ec);
    ctx.identical = hasIndex && hasMetadata &&
                    existingIndex == prepared.result.indexText &&
                    existingMetadata == prepared.metadataText;
    if (options.dryRun || !exists || ctx.identical || options.force) return true;
    std::fprintf(stderr,
                 "cp3c: native binding '%s' already exists at '%s'.\n"
                 "Re-run with --force to overwrite it.\n",
                 ctx.id.c_str(), ctx.target.string().c_str());
    return false;
}

bool writeBinding(const InstallContext& ctx, const PreparedBinding& prepared) {
    if (!writeTextFile(ctx.indexPath, prepared.result.indexText)) {
        std::fprintf(stderr, "cp3c: cannot write '%s'\n",
                     ctx.indexPath.string().c_str());
        return false;
    }
    if (!writeTextFile(ctx.metadataPath, prepared.metadataText)) {
        std::fprintf(stderr, "cp3c: cannot write '%s'\n",
                     ctx.metadataPath.string().c_str());
        return false;
    }
    return true;
}

}  // namespace

int runNativeInstall(const NativeOptions& options) {
    InstallContext ctx;
    if (!resolveContext(options, ctx)) return kExitUsageError;

    native::SdkRequest request;
    request.input = options.input;
    request.extraIncludes = options.includes;
    request.defines = options.defines;
    request.extraLibDirs = options.libDirs;
    request.links = options.links;

    native::SdkResolution sdk;
    std::string sdkError;
    if (!native::resolveSdk(request, sdk, sdkError)) {
        std::fprintf(stderr, "cp3c: %s\n", sdkError.c_str());
        return kExitUsageError;
    }
    if (options.verbose) printList("clang args", sdk.clangArgs);

    std::string libclangError;
    const native::LibClang lib = native::LibClang::load(libclangError);
    if (!lib.loaded) {
        std::fprintf(stderr, "cp3c: %s\n", libclangError.c_str());
        return kExitUsageError;
    }

    PreparedBinding prepared;
    if (!prepareBinding(ctx, sdk, lib, prepared)) return kExitCompileError;
    if (!checkOverwrite(options, ctx, prepared)) return kExitUsageError;

    reportInstall(options, sdk, prepared, ctx);
    if (options.dryRun || (ctx.identical && !options.force)) return kExitSuccess;
    if (!writeBinding(ctx, prepared)) return kExitCompileError;
    return kExitSuccess;
}

}  // namespace cp3::cli
