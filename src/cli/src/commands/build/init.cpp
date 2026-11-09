// =============================================================================
// src/cli/src/commands/init.cpp - `cp3c init [dir]`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdInit.
//
// Scaffolds `package.json`, `src/index.cp3` (the spec's default entry - see
// CLI-README §5.1) and, for `"type": "package"`, a README.md listed in
// `build.files`. On a TTY the questions are asked interactively and the two
// choice fields are arrow-key menus (the same selector the toolchain wizard
// uses); a pipe gets numbered choices. Something only a machine can drive,
// `--yes`, skips prompts entirely. The prompt order follows the spec:
// type -> name -> description -> author -> type-system pattern.
// `--type-system <value>`, `--description <value>` and `--author <value>`
// supply the third, fourth and fifth answers for scripts and non-TTY use.
//
// `package.json` parity with CLI-README §5.1: name, version, description,
// author (when given), type, main, typeSystem, the start/verify/build `scripts`
// block (CLI-README line 124) and a `build` block (`bundleNativeDlls` for
// applications, `files` for packages).
//
// Existing scaffold files are left unchanged and reported, unless `--force`
// (or `-f`) is given, in which case the three scaffold files are overwritten.
// Unrelated files are never touched.
// =============================================================================
#include <cstdio>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include "detail/core/commands.hpp"
#include "detail/core/util.hpp"
#include "detail/toolchain/toolchain_select.hpp"

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

// The standard scripts block, pointed at the project entry. npm-style
// `scripts` names never shadow cp3c built-ins here: `start`, `verify` and
// `build` stay script names, resolvable via `cp3c <script>`.
std::string scriptsBlock(const std::string& entry) {
    std::string out;
    out += "  \"scripts\": {\n";
    out += "    \"start\": \"cp3c run " + entry + "\",\n";
    out += "    \"verify\": \"cp3c check " + entry + "\",\n";
    out += "    \"build\": \"cp3c build " + entry + "\"\n";
    out += "  },\n";
    return out;
}

std::string packageJsonFor(const std::string& name,
                           const std::string& description,
                           const std::string& author,
                           const std::string& type,
                           const std::string& typeSystem) {
    const bool isPackage = (type == "package");
    const std::string entry = "src/index.cp3";
    std::string out;
    out += "{\n";
    out += "  \"name\": \"" + jsonEscape(name) + "\",\n";
    out += "  \"version\": \"1.0.0\",\n";
    out += "  \"description\": \"" + jsonEscape(description) + "\",\n";
    if (!author.empty()) {
        out += "  \"author\": \"" + jsonEscape(author) + "\",\n";
    }
    out += "  \"type\": \"" + jsonEscape(type) + "\",\n";
    out += "  \"main\": \"" + entry + "\",\n";
    out += "  \"typeSystem\": \"" + jsonEscape(typeSystem) + "\",\n";
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

// Arrow-key menu on a TTY, numbered fallback on a pipe. Returns the chosen
// row, or -1 when the user cancels with q/Esc.
int askMenu(const std::string& title, const std::vector<std::string>& rows,
            const std::vector<std::string>& keys, int initial,
            bool& cancelled) {
    return runMenu(title, rows, keys, initial, cancelled);
}

bool validTypeSystem(const std::string& value) {
    return value == "typescript" || value == "cpp" || value == "rust";
}

// Writes one scaffold file; a pre-existing file is left unchanged (and
// reported) unless `overwrite` is given. Returns false only on a write
// failure.
bool writeScaffold(const fs::path& path, const std::string& content,
                   bool overwrite) {
    std::error_code ec;
    const bool exists = fs::exists(path, ec);
    if (exists && !overwrite) {
        std::fprintf(stderr, "cp3c: %s already exists; leaving it unchanged\n",
                     path.string().c_str());
        return true;
    }
    if (exists) {
        std::fprintf(stderr, "cp3c: overwriting %s\n", path.string().c_str());
    }
    if (!writeTextFile(path, content, nullptr)) {
        std::fprintf(stderr, "cp3c: could not write %s\n", path.string().c_str());
        return false;
    }
    return true;
}

}  // namespace

int cmdInit(const CliOptions& opt) {
    if (opt.positionals.size() > 1) {
        std::fputs("cp3c: usage: cp3c init [dir] [--force|-f] [--yes] "
                   "[--type application|package] [--type-system typescript|cpp|rust]\n"
                   "        [--description <text>] [--author <who>]\n",
                   stderr);
        return kExitUsageError;
    }
    std::error_code ec;
    fs::path target = opt.positionals.empty() ? fs::current_path(ec)
                                              : fs::path(opt.positionals[0]);
    std::string name = target.filename().string();
    if (name.empty() || name == "." || name == "..") name = "cp3-app";
    std::string description =
        opt.initDescription.empty() ? "a C+++ project" : opt.initDescription;
    std::string author = opt.initAuthor;
    std::string type = opt.initType.empty() ? "application" : opt.initType;
    std::string typeSystem =
        opt.initTypeSystem.empty() ? "typescript" : opt.initTypeSystem;
    if (!validTypeSystem(typeSystem)) {
        std::fprintf(stderr,
                     "cp3c: invalid type system '%s' (expected typescript, "
                     "cpp or rust)\n",
                     typeSystem.c_str());
        return kExitUsageError;
    }

    if (!opt.yes && isTty(stdin)) {
        // Spec prompt order: type -> name -> description -> author ->
        // type-system (CLI-README §5.1). Menus with arrow keys on a TTY.
        bool cancelled = false;
        const int typeChoice = askMenu(
            "Type (application/package)",
            {"Application - a runnable binary",
             "Package - a library consumed via `cp3c install`"},
            {"application", "package"}, type == "package" ? 1 : 0, cancelled);
        if (cancelled) {
            std::fputs("cp3c: init cancelled; nothing written\n", stderr);
            return kExitUsageError;
        }
        type = typeChoice == 1 ? "package" : "application";

        name = prompt("Project name", name);
        description = prompt("Description", description);
        author = prompt("Author (name <mail@example.com>)", author);
        const int defaultTs =
            typeSystem == "cpp" ? 1 : (typeSystem == "rust" ? 2 : 0);
        cancelled = false;
        const int tsChoice = askMenu(
            "Type system",
            {"typescript - number stays IEEE-754 doubles",
             "cpp - i32/i64/u64 native defaults",
             "rust - i32/i64/u64 native defaults"},
            {"typescript", "cpp", "rust"}, defaultTs, cancelled);
        if (cancelled) {
            std::fputs("cp3c: init cancelled; nothing written\n", stderr);
            return kExitUsageError;
        }
        typeSystem = tsChoice == 1 ? "cpp" : tsChoice == 2 ? "rust" : "typescript";
    }
    const bool isPackage = (type == "package");

    if (!fs::create_directories(target / "src", ec) && ec) {
        std::fprintf(stderr, "cp3c: cannot create %s (%s)\n",
                     (target / "src").string().c_str(), ec.message().c_str());
        return kExitCompileError;
    }

    if (!writeScaffold(target / "package.json",
                       packageJsonFor(name, description, author, type,
                                      typeSystem),
                       opt.force)) {
        return kExitCompileError;
    }
    const fs::path entryFile = target / "src" / "index.cp3";
    const std::string entry =
        isPackage ? packageEntrySource(name) : appEntrySource(name);
    if (!writeScaffold(entryFile, entry, opt.force)) return kExitCompileError;
    if (isPackage &&
        !writeScaffold(target / "README.md", readmeSource(name, description),
                       opt.force)) {
        return kExitCompileError;
    }

    std::fprintf(stderr, "cp3c: initialized cp3 %s '%s' in %s\n",
                 isPackage ? "package" : "project", name.c_str(),
                 target.string().c_str());
    return kExitSuccess;
}

}  // namespace cp3::cli
