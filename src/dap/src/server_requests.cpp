// =============================================================================
// src/dap/src/server_requests.cpp - one handler per DAP request.
//
// OWNERSHIP: cp3-debug workstream (src/dap/**).
//
// The transport loop, dispatch table and event forwarding live in server.cpp;
// this unit owns the request semantics: launch (build + gdb), breakpoints,
// configurationDone, resume/pause, and the stack/scope/variable queries that
// make up the visible debugging surface.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cp3/dap.hpp"

#include "detail/server.hpp"
#include "detail/server_util.hpp"
#include "detail/session.hpp"

namespace cp3::dap {
namespace {

// The first non-empty line of a compiler log, for the launch error message.
std::string summaryLine(const std::string& log) {
    std::size_t start = 0;
    while (start < log.size()) {
        const std::size_t end = log.find('\n', start);
        const std::string line = log.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty()) return line;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return "build failed";
}

std::string baseName(const std::string& path) {
    const std::size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}

std::string currentDirectory() {
    std::error_code ec;
    const std::filesystem::path path = std::filesystem::current_path(ec);
    return ec ? std::string() : path.string();
}

void addScope(Json& scopes, const std::string& name, int reference) {
    Json scope = jsonObject();
    scope.set("name", jsonString(name));
    scope.set("variablesReference", jsonInt(reference));
    scope.set("expensive", jsonBool(false));
    scopes.push(std::move(scope));
}

}  // namespace

void Server::handleInitialize(const std::string& requestSeq) {
    Json capabilities = jsonObject();
    capabilities.set("supportsConfigurationDoneRequest", jsonBool(true));
    capabilities.set("supportsTerminateRequest", jsonBool(true));
    capabilities.set("supportTerminateDebuggee", jsonBool(true));
    sendResponse(requestSeq, "initialize", std::move(capabilities));
    sendEvent("initialized", Json());
}

void Server::handleLaunch(const std::string& requestSeq, const Json& arguments) {
    const std::string program = fieldString(arguments, "program");
    if (program.empty()) {
        sendErrorResponse(requestSeq, "launch",
                          "launch: the 'program' attribute is required");
        return;
    }
    std::string cwd = fieldString(arguments, "cwd");
    if (cwd.empty()) cwd = currentDirectory();
    if (session_ != nullptr) session_->terminate();
    session_.reset();
    const BuildOutcome outcome = build_(program, cwd);
    if (!outcome.log.empty()) sendOutput("console", outcome.log);
    if (!outcome.built) {
        sendErrorResponse(requestSeq, "launch",
                          "build failed: " + summaryLine(outcome.log));
        return;
    }
    auto session = std::make_unique<GdbSession>();
    const std::string launchError = session->launch(outcome.exePath, cwd);
    if (!launchError.empty()) {
        sendErrorResponse(requestSeq, "launch", launchError);
        return;
    }
    session_ = std::move(session);
    // The gdb pid goes to stderr (never the DAP stream); the hygiene part of
    // tests/dap/dap_check.py checks that this process is gone at exit.
    const std::string note = "cp3c dap: debugging " + outcome.exePath +
                             " with gdb pid " +
                             std::to_string(session_->gdbPid()) + "\n";
    std::fputs(note.c_str(), stderr);
    sendResponse(requestSeq, "launch", Json());
}

void Server::handleSetBreakpoints(const std::string& requestSeq,
                                  const Json& arguments) {
    const Json* source = arguments.find("source");
    const std::string path =
        source != nullptr ? fieldString(*source, "path") : std::string();
    if (path.empty()) {
        sendErrorResponse(requestSeq, "setBreakpoints",
                          "setBreakpoints: source.path is required");
        return;
    }
    std::vector<BreakpointRequest> requests;
    const Json* breakpoints = arguments.find("breakpoints");
    if (breakpoints != nullptr && breakpoints->isArray()) {
        for (const Json& item : breakpoints->items) {
            BreakpointRequest request;
            request.line = static_cast<std::uint32_t>(fieldInt(item, "line", 0));
            requests.push_back(request);
        }
    }
    Json result = jsonArray();
    GdbSession* session = liveSession();
    if (session == nullptr) {
        for (const BreakpointRequest& request : requests) {
            Json entry = jsonObject();
            entry.set("verified", jsonBool(false));
            entry.set("line", jsonInt(static_cast<std::int64_t>(request.line)));
            entry.set("message", jsonString("the debug session has not started"));
            result.push(std::move(entry));
        }
    } else {
        std::vector<BreakpointState> states;
        const std::string error =
            session->setBreakpoints(path, requests, states);
        if (!error.empty()) {
            sendErrorResponse(requestSeq, "setBreakpoints", error);
            return;
        }
        for (const BreakpointState& state : states) {
            Json entry = jsonObject();
            entry.set("verified", jsonBool(state.verified));
            entry.set("line", jsonInt(static_cast<std::int64_t>(state.line)));
            if (state.verified) entry.set("id", jsonInt(state.gdbNumber));
            if (!state.message.empty()) {
                entry.set("message", jsonString(state.message));
            }
            result.push(std::move(entry));
        }
    }
    Json body = jsonObject();
    body.set("breakpoints", std::move(result));
    sendResponse(requestSeq, "setBreakpoints", std::move(body));
}

void Server::handleConfigurationDone(const std::string& requestSeq) {
    GdbSession* session = liveSession();
    std::string error;
    if (session != nullptr && !session->running() && !session->stopped()) {
        error = session->start();
    }
    if (error.empty()) {
        sendResponse(requestSeq, "configurationDone", Json());
    } else {
        sendErrorResponse(requestSeq, "configurationDone", error);
    }
}

void Server::handleResume(const std::string& command,
                          const std::string& requestSeq,
                          const std::string& action) {
    GdbSession* session = liveSession();
    if (session == nullptr) {
        sendErrorResponse(requestSeq, command, "no active debug session");
        return;
    }
    const std::string error = session->resume(action);
    if (error.empty()) {
        sendResponse(requestSeq, command, Json());
    } else {
        sendErrorResponse(requestSeq, command, error);
    }
}

void Server::handlePause(const std::string& requestSeq) {
    GdbSession* session = liveSession();
    if (session == nullptr) {
        sendErrorResponse(requestSeq, "pause", "no active debug session");
        return;
    }
    const std::string error = session->pause();
    if (error.empty()) {
        sendResponse(requestSeq, "pause", Json());
    } else {
        sendErrorResponse(requestSeq, "pause", error);
    }
}

void Server::handleThreads(const std::string& requestSeq) {
    const int threadId = session_ != nullptr ? session_->mainThread() : 1;
    Json thread = jsonObject();
    thread.set("id", jsonInt(threadId));
    thread.set("name", jsonString("main"));
    Json threads = jsonArray();
    threads.push(std::move(thread));
    Json body = jsonObject();
    body.set("threads", std::move(threads));
    sendResponse(requestSeq, "threads", std::move(body));
}

void Server::handleStackTrace(const std::string& requestSeq) {
    GdbSession* session = liveSession();
    if (session == nullptr) {
        sendErrorResponse(requestSeq, "stackTrace", "no active debug session");
        return;
    }
    std::vector<FrameInfo> frames;
    const std::string error = session->stackTrace(frames);
    if (!error.empty()) {
        sendErrorResponse(requestSeq, "stackTrace", error);
        return;
    }
    Json stack = jsonArray();
    for (const FrameInfo& frame : frames) {
        Json entry = jsonObject();
        entry.set("id", jsonInt(frame.level + 1));
        if (!frame.function.empty()) {
            entry.set("name", jsonString(frame.function));
        }
        if (!frame.path.empty()) {
            Json source = jsonObject();
            source.set("name", jsonString(baseName(frame.path)));
            source.set("path", jsonString(frame.path));
            entry.set("source", std::move(source));
            entry.set("line", jsonInt(frame.line == 0 ? 1 : frame.line));
            entry.set("column", jsonInt(1));
        }
        stack.push(std::move(entry));
    }
    Json body = jsonObject();
    body.set("stackFrames", std::move(stack));
    body.set("totalFrames", jsonInt(static_cast<std::int64_t>(frames.size())));
    sendResponse(requestSeq, "stackTrace", std::move(body));
}

void Server::handleScopes(const std::string& requestSeq,
                          const Json& arguments) {
    GdbSession* session = liveSession();
    if (session == nullptr) {
        sendErrorResponse(requestSeq, "scopes", "no active debug session");
        return;
    }
    const int frameId = fieldInt(arguments, "frameId", 1);
    const int frame = frameId > 0 ? frameId - 1 : 0;
    Json scopes = jsonArray();
    addScope(scopes, "Arguments", session->createScope(frame, true));
    addScope(scopes, "Locals", session->createScope(frame, false));
    Json body = jsonObject();
    body.set("scopes", std::move(scopes));
    sendResponse(requestSeq, "scopes", std::move(body));
}

void Server::handleVariables(const std::string& requestSeq,
                             const Json& arguments) {
    GdbSession* session = liveSession();
    if (session == nullptr) {
        sendErrorResponse(requestSeq, "variables", "no active debug session");
        return;
    }
    const int reference = fieldInt(arguments, "variablesReference", 0);
    std::vector<VariableInfo> variables;
    const std::string error = session->variables(reference, variables);
    if (!error.empty()) {
        sendErrorResponse(requestSeq, "variables", error);
        return;
    }
    Json list = jsonArray();
    for (const VariableInfo& variable : variables) {
        Json entry = jsonObject();
        entry.set("name", jsonString(variable.name));
        entry.set("value", jsonString(variable.value));
        if (!variable.type.empty()) {
            entry.set("type", jsonString(variable.type));
        }
        entry.set("variablesReference", jsonInt(variable.reference));
        list.push(std::move(entry));
    }
    Json body = jsonObject();
    body.set("variables", std::move(list));
    sendResponse(requestSeq, "variables", std::move(body));
}

void Server::handleDisconnect(const std::string& requestSeq) {
    if (session_ != nullptr) session_->terminate();
    sendResponse(requestSeq, "disconnect", Json());
    done_ = true;
}

void Server::handleTerminate(const std::string& requestSeq) {
    if (session_ != nullptr) session_->terminate();
    sendResponse(requestSeq, "terminate", Json());
    sendEvent("terminated", Json());
}

}  // namespace cp3::dap
