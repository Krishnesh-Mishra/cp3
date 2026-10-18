// =============================================================================
// cli/commands_init.cpp - `cp3c init [dir]`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdInit.
//
// Scaffolds `package.json`, `src/index.cp3` (the spec's default entry - see
// CLI-README §5.1) and, for `"type": "package"`, a README.md listed in
// `build.files`. Interactive prompts appear only on a TTY and only when --yes
// is absent; the prompt order follows the spec: type -> name -> description
// (the type-system question is deliberately not asked yet - that lands with
// the native-types wave).
//
// `package.json` parity with CLI-README §5.1: name, version, description,
// type, main, typeSystem, the standard dev/build/start/check `scripts` block
// (CLI-README line 124) and a `build` block (`bundleNativeDlls` for
// applications, `files` for packages).
// =============================================================================
#include <cstdio>
#include <iostream>
#include <string>
#include <system_error>

#include "private/commands.hpp"
#include "private/util.hpp"

namespace cp3::cli {

namespace {

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

// The standard scripts block, pointed at the project entry (CLI-README §1,
// line 124: `cp3 init` generates dev/build/start/check out of the box).
std::string scriptsBlock(const std::string& entry) {
    std::string out;
    out += "  \"scripts\": {\n";
    out += "    \"dev\": \"cp3c dev " + entry + "\",\n";
    out += "    \"build\": \"cp3c build " + entry + "\",\n";
    out += "    \"start\": \"cp3c run " + entry + "\",\n";
    out += "    \"check\": \"cp3c check " + entry + "\"\n";
    out += "  },\n";
    return out;
}

std::string packageJsonFor(const std::string& name,
                           const std::string& description,
                           const std::string& type) {
    const bool isPackage = (type == "package");
    const std::string entry = "src/index.cp3";
    std::string out;
    out += "{\n";
    out += "  \"name\": \"" + jsonEscape(name) + "\",\n";
    out += "  \"version\": \"1.0.0\",\n";
    out += "  \"description\": \"" + jsonEscape(description) + "\",\n";
    out += "  \"type\": \"" + jsonEscape(type) + "\",\n";
    out += "  \"main\": \"" + entry + "\",\n";
    out += "  \"typeSystem\": \"typescript\",\n";
    out += scriptsBlock(entry);
    out += "  \"build\": {\n";
    if (isPackage) {
        // Extra files shipped inside the .cp3pak (CLI-README §5.1).
        out += "    \"files\": [\"README.md\"]\n";
    } else {
        // Application default (CLI-README §1/§5.1).
        out += "    \"bundleNativeDlls\": true\n";
    }
    out += "  }\n";
    out += "}\n";
    return out;
}

// Hello-world entry for an application.
std::string appEntrySource(const std::string& name) {
    std::string out;
    out += "import console from \"std/console\";\n";
    out += "\n";
    out += "console.log(\"Hello from " + name + "!\");\n";
    return out;
}

// Package entry: a library exports a single default object (CLI-README §5.2),
// and `export default { ... }` is mandatory for `"type": "package"` - so the
// scaffold starts from a valid package entry instead of a runnable program.
std::string packageEntrySource(const std::string& name) {
    std::string out;
    out += "// " + name + " - package entry; every key below is a named export.\n";
    out += "function add(a: number, b: number): number {\n";
    out += "  return a + b;\n";
    out += "}\n";
    out += "\n";
    out += "export default { add };\n";
    return out;
}

std::string readmeSource(const std::string& name,
                         const std::string& description) {
    std::string out;
    out += "# " + name + "\n";
    out += "\n";
    out += description + "\n";
    out += "\n";
    out += "Built with the C+++ (`cp3`) toolchain. Run `cp3c install` in a\n";
    out += "consuming project to use this package.\n";
    return out;
}

std::string prompt(const std::string& question, const std::string& fallback) {
    std::cout << question;
    if (!fallback.empty()) std::cout << " [" << fallback << "]";
    std::cout << ": " << std::flush;
    std::string answer;
    if (!std::getline(std::cin, answer)) return fallback;
    // trim
    const auto first = answer.find_first_not_of(" \t\r\n");
    const auto last = answer.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) return fallback;
    return answer.substr(first, last - first + 1);
}

// Writes one scaffold file; a pre-existing file is left unchanged (and
// reported) rather than clobbered. Returns false only on a write failure.
bool writeScaffold(const fs::path& path, const std::string& content) {
    std::error_code ec;
    if (fs::exists(path, ec)) {
        std::fprintf(stderr, "cp3c: %s already exists; leaving it unchanged\n",
                     path.string().c_str());
        return true;
    }
    if (!writeTextFile(path, content, nullptr)) {
        std::fprintf(stderr, "cp3c: could not write %s\n", path.string().c_str());
        return false;
    }
    return true;
}

}  // namespace

int cmdInit(const CliOptions& opt) {
    std::error_code ec;
    fs::path target = opt.positionals.empty() ? fs::current_path(ec)
                                              : fs::path(opt.positionals[0]);
    std::string name = target.filename().string();
    if (name.empty() || name == "." || name == "..") name = "cp3-app";
    std::string description = "a C+++ project";
    std::string type = opt.initType.empty() ? "application" : opt.initType;

    if (!opt.yes && isTty(stdin)) {
        // Spec prompt order: type -> name -> description (CLI-README §5.1).
        type = prompt("Type (application/package)", type);
        if (type != "application" && type != "package") type = "application";
        name = prompt("Project name", name);
        description = prompt("Description", description);
    }
    const bool isPackage = (type == "package");

    if (!fs::create_directories(target / "src", ec) && ec) {
        std::fprintf(stderr, "cp3c: cannot create %s (%s)\n",
                     (target / "src").string().c_str(), ec.message().c_str());
        return kExitCompileError;
    }

    if (!writeScaffold(target / "package.json",
                       packageJsonFor(name, description, type))) {
        return kExitCompileError;
    }
    const fs::path entryFile = target / "src" / "index.cp3";
    const std::string entry =
        isPackage ? packageEntrySource(name) : appEntrySource(name);
    if (!writeScaffold(entryFile, entry)) return kExitCompileError;
    if (isPackage &&
        !writeScaffold(target / "README.md", readmeSource(name, description))) {
        return kExitCompileError;
    }

    std::fprintf(stderr, "cp3c: initialized cp3 %s '%s' in %s\n",
                 isPackage ? "package" : "project", name.c_str(),
                 target.string().c_str());
    return kExitSuccess;
}

}  // namespace cp3::cli
