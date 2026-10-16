// =============================================================================
// cli/commands_init.cpp — `cp3c init [dir]`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::cmdInit.
//
// Scaffolds package.json + src/main.cp3. Interactive prompts appear only on a
// TTY and only when --yes is absent.
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

std::string packageJsonFor(const std::string& name,
                           const std::string& description,
                           const std::string& type) {
    std::string out;
    out += "{\n";
    out += "  \"name\": \"" + jsonEscape(name) + "\",\n";
    out += "  \"version\": \"1.0.0\",\n";
    out += "  \"description\": \"" + jsonEscape(description) + "\",\n";
    out += "  \"type\": \"" + jsonEscape(type) + "\",\n";
    out += "  \"main\": \"src/main.cp3\",\n";
    out += "  \"typeSystem\": \"typescript\",\n";
    out += "  \"scripts\": {\n";
    out += "    \"dev\": \"cp3c dev src/main.cp3\",\n";
    out += "    \"build\": \"cp3c build src/main.cp3\",\n";
    out += "    \"start\": \"cp3c run src/main.cp3\",\n";
    out += "    \"check\": \"cp3c check src/main.cp3\"\n";
    out += "  },\n";
    out += "  \"build\": {\n";
    out += "    \"bundleNativeDlls\": true\n";
    out += "  }\n";
    out += "}\n";
    return out;
}

std::string helloWorldSource(const std::string& name) {
    std::string out;
    out += "import console from \"std/console\";\n";
    out += "\n";
    out += "console.log(\"Hello from " + name + "!\");\n";
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

}  // namespace

int cmdInit(const CliOptions& opt) {
    std::error_code ec;
    fs::path target = opt.positionals.empty() ? fs::current_path(ec)
                                              : fs::path(opt.positionals[0]);
    std::string name = target.filename().string();
    if (name.empty() || name == "." || name == "..") name = "cp3-app";
    std::string description = "a C+++ project";
    std::string type = "application";

    if (!opt.yes && isTty(stdin)) {
        name = prompt("Project name", name);
        description = prompt("Description", description);
        type = prompt("Type (application/package)", type);
        if (type != "application" && type != "package") type = "application";
    }

    fs::create_directories(target / "src", ec);

    const fs::path pkg = target / "package.json";
    if (fs::exists(pkg, ec)) {
        std::fprintf(stderr, "cp3c: %s already exists; leaving it unchanged\n",
                     pkg.string().c_str());
    } else if (!writeTextFile(pkg, packageJsonFor(name, description, type),
                              nullptr)) {
        std::fprintf(stderr, "cp3c: could not write %s\n", pkg.string().c_str());
        return kExitCompileError;
    }

    const fs::path mainFile = target / "src" / "main.cp3";
    if (fs::exists(mainFile, ec)) {
        std::fprintf(stderr, "cp3c: %s already exists; leaving it unchanged\n",
                     mainFile.string().c_str());
    } else if (!writeTextFile(mainFile, helloWorldSource(name), nullptr)) {
        std::fprintf(stderr, "cp3c: could not write %s\n",
                     mainFile.string().c_str());
        return kExitCompileError;
    }

    std::fprintf(stderr, "cp3c: initialized cp3 project '%s' in %s\n",
                 name.c_str(), target.string().c_str());
    return kExitSuccess;
}

}  // namespace cp3::cli
