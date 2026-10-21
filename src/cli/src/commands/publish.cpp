// =============================================================================
// src/cli/src/commands/publish.cpp - `cp3c publish [<dir>]`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdPublish.
//
// Beta one publishes an artifact only: `publish` runs the same package build as
// `cp3c build`, then reports (and optionally copies with --out) the resulting
// .cp3pak. There is no registry server and no upload path in beta one.
// =============================================================================
#include <cstdio>
#include <string>
#include <system_error>

#include "cp3/cli.hpp"

#include "detail/checksum.hpp"
#include "detail/commands.hpp"
#include "detail/json.hpp"
#include "detail/package_json.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

namespace {

// Everything the report needs about one publish run.
struct PublishArtifact {
    std::string name;
    std::string version;
    fs::path entry;     // resolved package entry (.cp3)
    fs::path artifact;  // where the package build writes it
    fs::path output;    // where the user asked for a copy
    std::uintmax_t size = 0;
    std::string sha256;
    bool dryRun = false;
};

bool usableArtifactName(const std::string& name, std::string& err) {
    if (name.empty() || name == "." || name == ".." ||
        name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos) {
        err = "package name '" + name + "' cannot be an artifact file name";
        return false;
    }
    return true;
}

std::string artifactName(const std::string& name) { return name + ".cp3pak"; }

bool resolvePackageRoot(const std::string& positional, fs::path& root,
                        std::string& err) {
    std::error_code ec;
    fs::path candidate = positional.empty() ? fs::current_path(ec)
                                            : fs::path(positional);
    if (ec) candidate = ".";
    if (!fs::is_directory(candidate, ec)) {
        err = "'" + candidate.string() + "' is not a directory";
        return false;
    }
    root = fs::weakly_canonical(candidate, ec);
    if (ec) root = candidate;
    if (!fs::is_regular_file(root / "package.json", ec)) {
        err = "no package.json in " + root.string();
        return false;
    }
    return true;
}

bool packageEntryFile(const fs::path& root, const JsonValue& manifest,
                      fs::path& entry, std::string& err) {
    const std::string main = packageMain(manifest);
    entry = root / fs::path(main);
    std::error_code ec;
    if (!fs::is_regular_file(entry, ec)) {
        err = "package entry '" + main + "' does not exist";
        return false;
    }
    return true;
}

std::string absolutePath(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) absolute = path;
    return absolute.generic_string();
}

std::uintmax_t fileSize(const fs::path& path) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    return ec ? 0 : size;
}

// Resolves --out to a file path. An existing directory receives
// <name>.cp3pak; an existing file or a path ending in .cp3pak is the file
// itself; any other new path is created as a directory.
fs::path outputTarget(const std::string& outArg, const std::string& name,
                      bool createDirectory, std::string& err) {
    std::error_code ec;
    const fs::path out(outArg);
    if (fs::is_directory(out, ec)) return out / artifactName(name);
    if (fs::is_regular_file(out, ec)) return out;
    if (endsWith(outArg, ".cp3pak")) return out;
    if (createDirectory) {
        fs::create_directories(out, ec);
        if (ec) {
            err = "cannot create output directory " + out.string() + " (" +
                  ec.message() + ")";
            return {};
        }
    }
    return out / artifactName(name);
}

bool copyArtifact(const fs::path& source, const fs::path& target, bool force,
                  std::string& err) {
    std::error_code ec;
    if (fs::equivalent(source, target, ec)) return true;
    if (fs::exists(target, ec) && !force) {
        err = "'" + target.string() +
              "' already exists; pass --force to overwrite it";
        return false;
    }
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        err = "cannot copy to " + target.string() + " (" + ec.message() + ")";
        return false;
    }
    return true;
}

void printPublishJson(const PublishArtifact& info) {
    std::string out = "{";
    out += "\"command\":\"publish\"";
    out += ",\"name\":" + jsonQuote(info.name);
    out += ",\"version\":" + jsonQuote(info.version);
    out += ",\"artifact\":" + jsonQuote(absolutePath(info.artifact));
    out += ",\"output\":" + jsonQuote(absolutePath(info.output));
    out += ",\"size\":" + std::to_string(info.size);
    out += ",\"sha256\":" + jsonQuote(info.sha256);
    out += ",\"dryRun\":";
    out += info.dryRun ? "true" : "false";
    out += "}\n";
    std::fputs(out.c_str(), stdout);
}

void printPublishHuman(const PublishArtifact& info) {
    if (info.dryRun) {
        std::fprintf(stdout, "would build: %s\n",
                     absolutePath(info.artifact).c_str());
        if (info.artifact != info.output) {
            std::fprintf(stdout, "would copy to: %s\n",
                         absolutePath(info.output).c_str());
        }
    } else if (info.artifact != info.output) {
        std::fprintf(stdout, "artifact: %s\n",
                     absolutePath(info.artifact).c_str());
        std::fprintf(stdout, "output: %s\n",
                     absolutePath(info.output).c_str());
    } else {
        std::fprintf(stdout, "artifact: %s\n",
                     absolutePath(info.artifact).c_str());
    }
    std::fprintf(stdout, "name: %s\n", info.name.c_str());
    std::fprintf(stdout, "version: %s\n", info.version.c_str());
    std::fprintf(stdout, "size: %llu bytes\n",
                 static_cast<unsigned long long>(info.size));
    std::fprintf(stdout, "sha256: %s\n", info.sha256.c_str());
}

// Validates the project and resolves the build/output paths without touching
// the output. Returns a documented exit code.
int preparePublish(const CliOptions& opt, PublishArtifact& info) {
    const std::string positional =
        opt.positionals.empty() ? std::string() : opt.positionals[0];
    fs::path root;
    std::string err;
    if (!resolvePackageRoot(positional, root, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitUsageError;
    }
    JsonValue manifest;
    if (!loadPackageJson(root, manifest, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    const std::string* type = manifest.stringField("type");
    if (type == nullptr || *type != "package") {
        std::fprintf(stderr,
                     "cp3c: cannot publish %s: package.json \"type\" must be "
                     "\"package\" (this is an %s project)\n",
                     root.string().c_str(),
                     type == nullptr ? "application" : type->c_str());
        return kExitUsageError;
    }
    info.name = packageName(manifest, root);
    info.version = packageVersion(manifest);
    if (!usableArtifactName(info.name, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitUsageError;
    }
    if (!packageEntryFile(root, manifest, info.entry, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitCompileError;
    }
    info.artifact = root / ".cp3" / "out" / artifactName(info.name);
    info.output = info.artifact;
    if (!opt.outDir.empty()) {
        info.output = outputTarget(opt.outDir, info.name, !opt.dryRun, err);
        if (info.output.empty()) {
            std::fprintf(stderr, "cp3c: %s\n", err.c_str());
            return kExitCompileError;
        }
        if (!opt.dryRun) {
            std::error_code ec;
            if (fs::exists(info.output, ec) && !opt.force &&
                !fs::equivalent(info.artifact, info.output, ec)) {
                std::fprintf(stderr,
                             "cp3c: '%s' already exists; pass --force to "
                             "overwrite it\n",
                             info.output.string().c_str());
                return kExitUsageError;
            }
        }
    }
    return kExitSuccess;
}

int runPublish(const CliOptions& opt, PublishArtifact& info) {
    CliOptions buildOpt = opt;
    buildOpt.file = info.entry.string();
    buildOpt.positionals.clear();
    buildOpt.outDir.clear();
    buildOpt.json = false;
    buildOpt.dryRun = false;
    const int buildRc = cmdBuildOrRun(buildOpt, false);
    if (buildRc != kExitSuccess) return buildRc;

    std::string err;
    std::error_code ec;
    if (!fs::is_regular_file(info.artifact, ec)) {
        std::fprintf(stderr, "cp3c: the package build did not produce %s\n",
                     info.artifact.string().c_str());
        return kExitCompileError;
    }
    if (info.output != info.artifact &&
        !copyArtifact(info.artifact, info.output, opt.force, err)) {
        std::fprintf(stderr, "cp3c: %s\n", err.c_str());
        return kExitUsageError;
    }
    const fs::path artifactToHash =
        info.output != info.artifact ? info.output : info.artifact;
    std::string bytes;
    if (!readTextFile(artifactToHash, bytes)) {
        std::fprintf(stderr, "cp3c: cannot read %s for hashing\n",
                     artifactToHash.string().c_str());
        return kExitCompileError;
    }
    info.sha256 = sha256Hex(bytes);
    info.size = fileSize(artifactToHash);
    return kExitSuccess;
}

}  // namespace

int cmdPublish(const CliOptions& opt) {
    PublishArtifact info;
    info.dryRun = opt.dryRun;
    const int prepared = preparePublish(opt, info);
    if (prepared != kExitSuccess) return prepared;

    if (opt.dryRun) {
        if (opt.json) {
            printPublishJson(info);
        } else {
            std::fprintf(stderr,
                         "cp3c: dry run: no .cp3pak was built (registry "
                         "publishing is not part of beta one)\n");
            printPublishHuman(info);
        }
        return kExitSuccess;
    }

    const int built = runPublish(opt, info);
    if (built != kExitSuccess) return built;
    if (opt.json) {
        printPublishJson(info);
    } else {
        std::fprintf(stderr,
                     "cp3c: published %s %s (artifact only; beta one has no "
                     "registry)\n",
                     info.name.c_str(), info.version.c_str());
        printPublishHuman(info);
    }
    return kExitSuccess;
}

}  // namespace cp3::cli
