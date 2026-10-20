// =============================================================================
// src/cli/src/commands/toolchain_json.cpp - `cp3c toolchain list --json`.
//
// OWNERSHIP: cli module (src/cli/**). Implements cp3::cli::toolchainListJson.
// The human list stays in toolchain.cpp; this file owns only the stable,
// script-facing document: candidates, installed versions, both scopes and the
// effective choice.
// =============================================================================
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "detail/commands.hpp"
#include "detail/json.hpp"
#include "detail/package_json.hpp"
#include "detail/toolchain.hpp"
#include "detail/toolchain_select.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

namespace {

namespace fs = std::filesystem;

const char* sourceName(ToolchainSource source) {
    switch (source) {
        case ToolchainSource::Flag: return "command-line";
        case ToolchainSource::Environment: return "environment";
        case ToolchainSource::Project: return "project";
        case ToolchainSource::Global: return "global";
        case ToolchainSource::Auto: return "auto-detect";
        case ToolchainSource::None: break;
    }
    return "none";
}

std::string statusText(const CandidateStatus& status) {
    if (status.usable) return "ready";
    if (status.found) return "not usable";
    return "missing";
}

void appendStatusJson(std::string& out, const CandidateStatus& status) {
    out += "{\"id\": " + jsonQuote(status.candidate.id);
    out += ", \"name\": " + jsonQuote(status.candidate.name);
    out += ", \"status\": " + jsonQuote(statusText(status));
    out += ", \"found\": ";
    out += status.found ? "true" : "false";
    out += ", \"usable\": ";
    out += status.usable ? "true" : "false";
    out += ", \"program\": " + jsonQuote(status.program);
    out += ", \"version\": " + jsonQuote(status.version);
    out += ", \"problem\": " + jsonQuote(status.problem);
    out += "}";
}

void appendInstalledJson(std::string& out, const ToolchainCandidate& candidate,
                         const ToolchainInstall& install) {
    out += "{\"id\": " + jsonQuote(candidate.id);
    out += ", \"version\": " + jsonQuote(install.version);
    out += ", \"program\": " + jsonQuote(install.program);
    out += ", \"usable\": ";
    out += install.usable ? "true" : "false";
    if (!install.problem.empty()) {
        out += ", \"problem\": " + jsonQuote(install.problem);
    }
    out += "}";
}

void appendChoiceJson(std::string& out, const ToolchainChoice& choice) {
    out += "{\"present\": ";
    out += choice.present ? "true" : "false";
    if (choice.present) {
        out += ", \"id\": " + jsonQuote(choice.id);
        out += ", \"version\": " + jsonQuote(choice.version);
        out += ", \"policy\": " + jsonQuote(choice.policy);
        out += ", \"program\": " + jsonQuote(choice.program);
        out += ", \"custom\": ";
        out += choice.custom ? "true" : "false";
    }
    out += "}";
}

void appendCandidates(std::string& out) {
    out += "  \"candidates\": [";
    bool first = true;
    for (const ToolchainCandidate& candidate : toolchainCandidates()) {
        if (!first) out += ", ";
        first = false;
        appendStatusJson(out, probeToolchainCandidate(candidate, true));
    }
    out += "],\n";
}

void appendInstalled(std::string& out) {
    out += "  \"installed\": [";
    bool first = true;
    for (const ToolchainCandidate& candidate : toolchainCandidates()) {
        for (const ToolchainInstall& install :
             discoverToolchainVersions(candidate, false)) {
            if (!first) out += ", ";
            first = false;
            appendInstalledJson(out, candidate, install);
        }
    }
    out += "],\n";
}

void appendGlobal(std::string& out, const GlobalToolchainConfig& config) {
    out += "  \"global\": {\"path\": " +
           jsonQuote(toolchainConfigPath().string());
    out += ", \"present\": ";
    out += config.present ? "true" : "false";
    if (config.present) {
        out += ", \"choice\": ";
        appendChoiceJson(out, config.choice());
        out += ", \"saved\": " + jsonQuote(config.saved);
    }
    out += "},\n";
}

struct ProjectSummary {
    fs::path root;
    ToolchainChoice choice;
    std::string error;
    bool inProject = false;
    bool valid = false;
};

void appendProject(std::string& out, const ProjectSummary& project) {
    out += "  \"project\": {\"inProject\": ";
    out += project.inProject ? "true" : "false";
    if (project.inProject) {
        out += ", \"path\": " +
               jsonQuote((project.root / "package.json").string());
        out += ", \"valid\": ";
        out += project.valid ? "true" : "false";
        if (project.valid) {
            out += ", \"choice\": ";
            appendChoiceJson(out, project.choice);
        } else {
            out += ", \"error\": " + jsonQuote(project.error);
        }
    }
    out += "},\n";
}

void appendCurrent(std::string& out, const Toolchain& toolchain) {
    out += "  \"current\": {\"found\": ";
    out += toolchain.found() ? "true" : "false";
    out += ", \"source\": " + jsonQuote(sourceName(toolchainSource()));
    if (!toolchain.found()) {
        out += "},\n";
        return;
    }
    out += ", \"id\": " + jsonQuote(toolchain.id);
    out += ", \"name\": " + jsonQuote(toolchain.name);
    out += ", \"program\": " + jsonQuote(toolchain.program);
    out += ", \"version\": " + jsonQuote(toolchain.version);
    out += ", \"recorded\": " + jsonQuote(toolchain.recordedVersion);
    out += ", \"policy\": " + jsonQuote(toolchain.policy);
    if (toolchainSource() == ToolchainSource::Flag ||
        toolchainSource() == ToolchainSource::Environment) {
        // The origin is shown in parentheses by toolchainSourceDescription.
        const std::string description = toolchainSourceDescription();
        const std::size_t open = description.find('(');
        const std::size_t close = description.rfind(')');
        if (open != std::string::npos && close != std::string::npos &&
            close > open) {
            out += ", \"origin\": " +
                   jsonQuote(description.substr(open + 1, close - open - 1));
        }
    }
    out += "},\n";
}

}  // namespace

int toolchainListJson() {
    std::string out = "{\n  \"ok\": true,\n  \"action\": \"toolchain-list\",\n";
    appendCandidates(out);
    appendInstalled(out);
    const GlobalToolchainConfig config = readGlobalToolchainConfig();
    appendGlobal(out, config);

    ProjectSummary project;
    project.inProject = toolchainProjectRoot(project.root);
    project.valid =
        project.inProject &&
        readProjectToolchain(project.root, project.choice, project.error);
    appendProject(out, project);

    const Toolchain& toolchain = detectToolchain();
    appendCurrent(out, toolchain);
    out += "  \"versions\": {\"policy\": " +
           jsonQuote(toolchain.policy.empty() ? "auto" : toolchain.policy);
    out += ", \"recorded\": " + jsonQuote(toolchain.recordedVersion);
    out += ", \"effective\": " + jsonQuote(toolchain.version);
    out += "}\n}\n";
    std::fputs(out.c_str(), stdout);

    if (config.present) {
        warnSavedToolchainChoice(config.choice(), "global config");
    }
    if (project.valid && project.choice.present) {
        warnSavedToolchainChoice(project.choice, "project package.json");
    }
    return kExitSuccess;
}

}  // namespace cp3::cli
