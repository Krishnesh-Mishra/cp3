// =============================================================================
// src/cli/src/commands/editor_process.cpp - editor CLI argv + quiet execution.
//
// OWNERSHIP: cli module (src/cli/**). Implements the process half of
// detail/editor.hpp. Windows `.cmd`/`.bat` entry points are launched through
// `cmd.exe /c` so CreateProcess/_spawnvp can run them.
// =============================================================================
#include "detail/editor/editor.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <fcntl.h>
#  include <io.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace cp3::cli {

namespace {

bool endsWithNoCase(const std::string& text, const char* suffix) {
    const std::size_t length = std::strlen(suffix);
    if (text.size() < length) return false;
    for (std::size_t i = 0; i < length; ++i) {
        char a = text[text.size() - length + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

bool needsCommandShell(const std::string& cli) {
#if defined(_WIN32)
    return endsWithNoCase(cli, ".cmd") || endsWithNoCase(cli, ".bat");
#else
    (void)cli;
    return false;
#endif
}

#if defined(_WIN32)
// `_spawnvp` joins argv without quoting it, so `cmd.exe /c` would split a
// `.cmd` path that contains spaces (the standard VS Code install path does).
// Collapse the logical editor argv into one pre-quoted command string; the
// extra outer pair makes cmd.exe keep the inner quotes:
//   cmd.exe /c ""C:\Program Files\...\code.cmd" "--install-extension" "..."".
std::string quoteCmdPart(const std::string& arg) {
    std::string out = "\"";
    for (const char c : arg) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

std::vector<std::string> cmdShellCommand(const std::vector<std::string>& args) {
    std::string inner;
    for (std::size_t i = 2; i < args.size(); ++i) {
        if (!inner.empty()) inner.push_back(' ');
        inner += quoteCmdPart(args[i]);
    }
    std::vector<std::string> command;
    command.push_back(args[0]);
    command.push_back(args[1]);
    command.push_back("\"" + inner + "\"");
    return command;
}
#endif

}  // namespace

std::vector<std::string> editorCommand(const std::string& cli,
                                       const std::vector<std::string>& args) {
    std::vector<std::string> command;
    command.reserve(args.size() + 3);
    if (needsCommandShell(cli)) {
        command.push_back("cmd.exe");
        command.push_back("/c");
    }
    command.push_back(cli);
    command.insert(command.end(), args.begin(), args.end());
    return command;
}

int runEditorCommand(const std::vector<std::string>& command) {
    return runProcessQuiet(command);
}

int runProcessQuiet(const std::vector<std::string>& args) {
    std::fflush(stdout);
    std::fflush(stderr);
#if defined(_WIN32)
    const int outFd = _fileno(stdout);
    const int errFd = _fileno(stderr);
    const int savedOut = outFd >= 0 ? _dup(outFd) : -1;
    const int savedErr = errFd >= 0 ? _dup(errFd) : -1;
    const int nullFd = _open("NUL", _O_WRONLY);
    if (nullFd >= 0) {
        if (outFd >= 0) _dup2(nullFd, outFd);
        if (errFd >= 0) _dup2(nullFd, errFd);
        _close(nullFd);
    }
    const std::vector<std::string> invocation =
        args.size() >= 3 && endsWithNoCase(args[0], "cmd.exe") && args[1] == "/c"
            ? cmdShellCommand(args)
            : args;
    const int result = runProcess(invocation);
    if (savedOut >= 0) {
        _dup2(savedOut, outFd);
        _close(savedOut);
    }
    if (savedErr >= 0) {
        _dup2(savedErr, errFd);
        _close(savedErr);
    }
    std::fflush(stdout);
    std::fflush(stderr);
    return result;
#else
    const int savedOut = dup(STDOUT_FILENO);
    const int savedErr = dup(STDERR_FILENO);
    const int nullFd = open("/dev/null", O_WRONLY);
    if (nullFd >= 0) {
        dup2(nullFd, STDOUT_FILENO);
        dup2(nullFd, STDERR_FILENO);
        close(nullFd);
    }
    const int result = runProcess(args);
    if (savedOut >= 0) {
        dup2(savedOut, STDOUT_FILENO);
        close(savedOut);
    }
    if (savedErr >= 0) {
        dup2(savedErr, STDERR_FILENO);
        close(savedErr);
    }
    std::fflush(stdout);
    std::fflush(stderr);
    return result;
#endif
}

}  // namespace cp3::cli
