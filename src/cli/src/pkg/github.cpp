// =============================================================================
// src/cli/src/pkg/github.cpp - GitHub release discovery/download DEFINITIONS.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/github.hpp.
//
// No third-party dependency: the system curl is invoked through runProcess
// with an argv vector (no shell), so tokens and URLs are never re-parsed by a
// shell and the token is never printed.
// =============================================================================
#include "detail/github.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "detail/json.hpp"
#include "detail/util.hpp"

namespace cp3::cli {

namespace {

std::string trimTrailingSlashes(std::string text) {
    while (!text.empty() && text.back() == '/') text.pop_back();
    return text;
}

bool validGithubName(const std::string& text) {
    if (text.empty() || text == "." || text == "..") return false;
    for (char c : text) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                        c == '.';
        if (!ok) return false;
    }
    return true;
}

std::string tempJsonPath() {
    static unsigned counter = 0;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec);
    if (ec) dir = ".";
    return (dir / ("cp3c-github-" + std::to_string(stamp) + "-" +
                   std::to_string(++counter) + ".json"))
        .string();
}

bool findCurl(std::string& curl, std::string& err) {
    if (findExecutable("curl", curl)) return true;
    err =
        "downloading GitHub release assets needs the system HTTP client "
        "(curl.exe on Windows, curl on POSIX), which was not found on PATH";
    return false;
}

void appendHeader(std::vector<std::string>& args, const std::string& header) {
    args.push_back("-H");
    args.push_back(header);
}

#if defined(_WIN32)
// The CRT `_spawnvp` joins argv with spaces without quoting, which would split
// an HTTP header such as "Accept: application/json" into several arguments.
// Build the command line ourselves and quote it for CreateProcessA.
std::string quoteWindowsArg(const std::string& arg) {
    std::string out = "\"";
    std::size_t backslashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
            backslashes = 0;
            continue;
        }
        out.append(backslashes, '\\');
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}

std::string windowsCommandLine(const std::vector<std::string>& args) {
    std::string out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) out.push_back(' ');
        out += quoteWindowsArg(args[i]);
    }
    return out;
}

int spawnCurl(const std::vector<std::string>& args, std::string& err) {
    std::string line = windowsCommandLine(args);
    std::vector<char> mutableLine(line.begin(), line.end());
    mutableLine.push_back('\0');
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(nullptr, mutableLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &process)) {
        err = "cannot start the system HTTP client (Windows error " +
              std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
        return -1;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}
#else
int spawnCurl(const std::vector<std::string>& args, std::string& err) {
    (void)err;
    return runProcess(args);
}
#endif

bool runCurl(const std::vector<std::string>& args, const std::string& url,
             bool failOnHttpError, std::string& err) {
    std::string curl;
    if (!findCurl(curl, err)) return false;
    std::vector<std::string> command;
    command.push_back(curl);
    command.push_back("-sS");
    command.push_back("-L");
    if (failOnHttpError) command.push_back("-f");
    command.push_back("--connect-timeout");
    command.push_back("15");
    command.push_back("--max-time");
    command.push_back("300");
    command.push_back("--retry");
    command.push_back("2");
    for (const std::string& arg : args) command.push_back(arg);
    command.push_back(url);
    const int rc = spawnCurl(command, err);
    if (rc == 0) return true;
    if (err.empty()) {
        err = "download failed for '" + url + "' (curl exit " +
              std::to_string(rc) + ")";
    }
    return false;
}

bool downloadApiJson(const std::string& url, const std::string& dest,
                     std::string& err) {
    std::vector<std::string> args;
    args.push_back("-o");
    args.push_back(dest);
    appendHeader(args, "Accept: application/vnd.github+json");
    appendHeader(args, "User-Agent: cp3c");
    const char* token = std::getenv("CP3_GITHUB_TOKEN");
    if (token != nullptr && token[0] != '\0') {
        appendHeader(args, std::string("Authorization: Bearer ") + token);
    }
    return runCurl(args, url, false, err);
}

bool releaseFromJson(const std::string& text, GithubRelease& out,
                     std::string& err) {
    JsonValue doc;
    if (!JsonValue::parse(text, doc, err)) {
        err = "GitHub API returned invalid JSON: " + err;
        return false;
    }
    const JsonValue* release = &doc;
    if (doc.isArray()) {
        // `/releases` style response: pick the first non-prerelease entry.
        release = nullptr;
        for (const JsonValue& item : doc.array) {
            const JsonValue* pre = item.find("prerelease");
            const bool isPre = pre != nullptr && pre->type == JsonValue::Type::Bool &&
                               pre->boolean;
            if (!isPre && item.stringField("tag_name") != nullptr) {
                release = &item;
                break;
            }
        }
        if (release == nullptr) {
            err = "GitHub repository has no non-prerelease release";
            return false;
        }
    }
    if (const std::string* message = release->stringField("message")) {
        if (release->stringField("tag_name") == nullptr) {
            err = "GitHub API: " + *message;
            return false;
        }
    }
    const std::string* tag = release->stringField("tag_name");
    if (tag == nullptr || tag->empty()) {
        err = "GitHub API response has no release tag";
        return false;
    }
    out.tag = *tag;
    out.assets.clear();
    const JsonValue* assets = release->arrayField("assets");
    if (assets == nullptr) return true;
    for (const JsonValue& item : assets->array) {
        if (!item.isObject()) continue;
        const std::string* name = item.stringField("name");
        const std::string* url = item.stringField("browser_download_url");
        if (url == nullptr) url = item.stringField("url");
        if (name == nullptr || name->empty() || url == nullptr || url->empty()) {
            continue;
        }
        out.assets.push_back(GithubAsset{*name, *url});
    }
    return true;
}

}  // namespace

std::string githubApiBase() {
    const char* env = std::getenv("CP3_GITHUB_API_URL");
    if (env != nullptr && env[0] != '\0') return trimTrailingSlashes(env);
    return "https://api.github.com";
}

bool parseGithubRepoUrl(const std::string& text, GithubRepo& out,
                        std::string& err) {
    const std::string prefix = "https://github.com/";
    if (text.rfind(prefix, 0) != 0) {
        err = "'" + text +
              "' is not a GitHub repository URL "
              "(expected https://github.com/<owner>/<repo>)";
        return false;
    }
    std::string rest = text.substr(prefix.size());
    if (rest.find('?') != std::string::npos ||
        rest.find('#') != std::string::npos) {
        err = "GitHub repository URLs must not carry a query or fragment";
        return false;
    }
    while (!rest.empty() && rest.back() == '/') rest.pop_back();
    if (rest.empty()) {
        err = "'" + text + "' names no repository";
        return false;
    }
    const std::size_t slash = rest.find('/');
    if (slash == std::string::npos) {
        err = "'" + text + "' names no repository (expected owner/repo)";
        return false;
    }
    std::string owner = rest.substr(0, slash);
    std::string repo = rest.substr(slash + 1);
    if (repo.find('/') != std::string::npos) {
        err = "'" + text +
              "' points inside a repository; expected "
              "https://github.com/<owner>/<repo>";
        return false;
    }
    if (endsWith(repo, ".git")) repo = repo.substr(0, repo.size() - 4);
    if (!validGithubName(owner) || !validGithubName(repo)) {
        err = "'" + text + "' is not a valid owner/repo pair";
        return false;
    }
    out.owner = owner;
    out.repo = repo;
    return true;
}

bool fetchLatestGithubRelease(const GithubRepo& repo, GithubRelease& out,
                              std::string& err) {
    const std::string base = githubApiBase();
    if (base.rfind("http://", 0) != 0 && base.rfind("https://", 0) != 0) {
        err = "CP3_GITHUB_API_URL must start with http:// or https://";
        return false;
    }
    const std::string url =
        base + "/repos/" + repo.owner + "/" + repo.repo + "/releases/latest";
    const std::string temp = tempJsonPath();
    if (!downloadApiJson(url, temp, err)) {
        std::error_code ec;
        fs::remove(temp, ec);
        return false;
    }
    std::string text;
    const bool read = readTextFile(temp, text);
    std::error_code ec;
    fs::remove(temp, ec);
    if (!read) {
        err = "cannot read the GitHub API response for " + repo.fullName();
        return false;
    }
    return releaseFromJson(text, out, err);
}

bool downloadGithubAsset(const std::string& url, const fs::path& dest,
                         std::string& err) {
    std::vector<std::string> args;
    args.push_back("-o");
    args.push_back(dest.string());
    appendHeader(args, "User-Agent: cp3c");
    if (!runCurl(args, url, true, err)) {
        std::error_code ec;
        fs::remove(dest, ec);
        return false;
    }
    std::error_code ec;
    if (!fs::is_regular_file(dest, ec)) {
        err = "download of '" + url + "' produced no file";
        return false;
    }
    return true;
}

}  // namespace cp3::cli
