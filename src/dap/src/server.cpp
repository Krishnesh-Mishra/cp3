// =============================================================================
// src/dap/src/server.cpp - the DAP server over stdio: loop, dispatch, events.
//
// OWNERSHIP: cp3-debug workstream (src/dap/**).
//
// One client, one stdio transport, one gdb session. The loop is a poll: while
// the client is quiet it pumps gdb, so `*stopped` events reach the editor even
// though nothing new arrived on stdin. stdout carries DAP frames only; the
// adapter's own diagnostics (and the gdb pid the hygiene test looks for) go to
// stderr.
//
// The request handlers live in server_requests.cpp; see detail/server.hpp.
// =============================================================================
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cp3/dap.hpp"

#include "detail/dap_framing.hpp"
#include "detail/server.hpp"
#include "detail/server_util.hpp"

namespace cp3::dap {
namespace {

constexpr int kExitClean = 0;
constexpr int kExitProtocol = 2;
constexpr int kIdlePumpMs = 2;

// Keeps a number lexeme exactly as the client wrote it (request ids).
Json jsonNumberLexeme(std::string lexeme) {
    Json value;
    value.kind = Json::Kind::Number;
    value.number = std::move(lexeme);
    return value;
}

const char* stopReasonName(StopKind kind) {
    switch (kind) {
        case StopKind::Breakpoint: return "breakpoint";
        case StopKind::Step: return "step";
        case StopKind::Pause: return "pause";
        case StopKind::Exception: return "exception";
        case StopKind::Entry: return "entry";
        case StopKind::Unknown: break;
    }
    return "unknown";
}

}  // namespace

int Server::run() {
    setBinaryStdio();
    while (!done_) {
        if (stdinHasData()) {
            if (!readOne()) return pendingExit_;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(kIdlePumpMs));
        }
        pumpSession();
    }
    return kExitClean;
}

// Reads and dispatches one frame; false with `pendingExit_` set when the
// stream is gone or can no longer be trusted.
bool Server::readOne() {
    std::string body;
    std::string err;
    if (!readFrame(stdin, body, err)) {
        if (session_ != nullptr) session_->terminate();
        if (err == kEofReason) {
            pendingExit_ = kExitClean;
        } else {
            std::fprintf(stderr, "cp3c dap: %s\n", err.c_str());
            pendingExit_ = kExitProtocol;
        }
        return false;
    }
    Json message;
    std::string parseError;
    if (parseJson(body, message, parseError)) handleMessage(message);
    // A malformed message has no id to answer; it is dropped.
    return true;
}

void Server::writeJson(const Json& message) {
    if (!writeFrame(stdout, toJsonText(message))) done_ = true;
}

void Server::sendResponse(const std::string& requestSeq,
                          const std::string& command, Json body) {
    Json response = jsonObject();
    response.set("seq", jsonInt(nextSeq_++));
    response.set("type", jsonString("response"));
    response.set("request_seq", jsonNumberLexeme(requestSeq));
    response.set("success", jsonBool(true));
    response.set("command", jsonString(command));
    if (!body.isNull()) response.set("body", std::move(body));
    writeJson(response);
}

void Server::sendErrorResponse(const std::string& requestSeq,
                               const std::string& command,
                               const std::string& message) {
    Json response = jsonObject();
    response.set("seq", jsonInt(nextSeq_++));
    response.set("type", jsonString("response"));
    response.set("request_seq", jsonNumberLexeme(requestSeq));
    response.set("success", jsonBool(false));
    response.set("command", jsonString(command));
    response.set("message", jsonString(message));
    Json error = jsonObject();
    error.set("id", jsonInt(1));
    error.set("format", jsonString(message));
    error.set("showUser", jsonBool(true));
    Json body = jsonObject();
    body.set("error", std::move(error));
    response.set("body", std::move(body));
    writeJson(response);
}

void Server::sendEvent(const std::string& name, Json body) {
    Json event = jsonObject();
    event.set("seq", jsonInt(nextSeq_++));
    event.set("type", jsonString("event"));
    event.set("event", jsonString(name));
    if (!body.isNull()) event.set("body", std::move(body));
    writeJson(event);
}

void Server::sendOutput(const std::string& category, const std::string& text) {
    Json body = jsonObject();
    body.set("category", jsonString(category));
    body.set("output", jsonString(text));
    sendEvent("output", std::move(body));
}

void Server::handleMessage(const Json& message) {
    if (fieldString(message, "type") != "request") return;
    const std::string command = fieldString(message, "command");
    if (command.empty()) return;
    const Json* arguments = message.find("arguments");
    dispatch(command, requestSeqText(message),
             arguments != nullptr ? *arguments : Json());
}

void Server::dispatch(const std::string& command, const std::string& requestSeq,
                      const Json& arguments) {
    if (command == "initialize") return handleInitialize(requestSeq);
    if (command == "launch") return handleLaunch(requestSeq, arguments);
    if (command == "setBreakpoints") {
        return handleSetBreakpoints(requestSeq, arguments);
    }
    if (command == "setExceptionBreakpoints" ||
        command == "setFunctionBreakpoints" ||
        command == "setInstructionBreakpoints" ||
        command == "setDataBreakpoints") {
        // Nothing to honour; answering keeps the client's setup moving.
        sendResponse(requestSeq, command, jsonObject());
        return;
    }
    if (command == "configurationDone") {
        return handleConfigurationDone(requestSeq);
    }
    if (command == "continue") return handleResume(command, requestSeq, "continue");
    if (command == "next") return handleResume(command, requestSeq, "next");
    if (command == "stepIn") return handleResume(command, requestSeq, "stepIn");
    if (command == "stepOut") return handleResume(command, requestSeq, "stepOut");
    if (command == "pause") return handlePause(requestSeq);
    if (command == "threads") return handleThreads(requestSeq);
    if (command == "stackTrace") return handleStackTrace(requestSeq);
    if (command == "scopes") return handleScopes(requestSeq, arguments);
    if (command == "variables") return handleVariables(requestSeq, arguments);
    if (command == "disconnect") return handleDisconnect(requestSeq);
    if (command == "terminate") return handleTerminate(requestSeq);
    sendErrorResponse(requestSeq, command, "unsupported request: " + command);
}

GdbSession* Server::liveSession() const {
    return session_ != nullptr && session_->active() ? session_.get() : nullptr;
}

void Server::pumpSession() {
    if (session_ == nullptr) return;
    session_->pump();
    std::vector<SessionEvent> events;
    session_->takeEvents(events);
    for (const SessionEvent& event : events) forwardEvent(event);
}

void Server::forwardEvent(const SessionEvent& event) {
    switch (event.kind) {
        case SessionEvent::Kind::Output:
            sendOutput(event.category, event.text);
            return;
        case SessionEvent::Kind::Stopped: {
            Json body = jsonObject();
            body.set("reason", jsonString(stopReasonName(event.stop)));
            body.set("threadId", jsonInt(event.threadId));
            body.set("allThreadsStopped", jsonBool(true));
            if (!event.breakpoints.empty()) {
                Json ids = jsonArray();
                for (const int number : event.breakpoints) {
                    ids.push(jsonInt(number));
                }
                body.set("hitBreakpointIds", std::move(ids));
            }
            sendEvent("stopped", std::move(body));
            return;
        }
        case SessionEvent::Kind::Continued: {
            Json body = jsonObject();
            body.set("threadId", jsonInt(event.threadId));
            body.set("allThreadsContinued", jsonBool(true));
            sendEvent("continued", std::move(body));
            return;
        }
        case SessionEvent::Kind::Exited: {
            Json body = jsonObject();
            body.set("exitCode", jsonInt(event.exitCode));
            sendEvent("exited", std::move(body));
            return;
        }
        case SessionEvent::Kind::Terminated:
            sendEvent("terminated", Json());
            return;
    }
}

int runStdioServer(const BuildFn& build) {
    Server server(build);
    return server.run();
}

}  // namespace cp3::dap
