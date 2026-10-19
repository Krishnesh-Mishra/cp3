// =============================================================================
// src/lsp/src/server.cpp - the `cp3c lsp` server loop and JSON-RPC dispatch.
//
// OWNERSHIP: LSP workstream (src/lsp/**). Implements cp3::lsp::runStdioServer.
//
// Single-threaded on purpose: the loop reads a frame, handles it, then (when
// an edit left work behind) coalesces any already-buffered frames so a burst
// of keystrokes costs one check per document. Only this loop writes stdout, so
// frames can never interleave; the checker runs in-process and reports through
// a sink, so it never prints at all.
// =============================================================================
#include "cp3/lsp.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cp3/diagnostic_codes.hpp"

#include "detail/framing.hpp"
#include "detail/lsp_json.hpp"
#include "detail/lsp_protocol.hpp"
#include "detail/lsp_text.hpp"

namespace cp3::lsp {
namespace {

// A keystroke burst is coalesced for at most this long before the pending
// check runs; the loop stops early as soon as input dries up.
constexpr auto kCoalesceWindow = std::chrono::milliseconds(120);

// JSON-RPC 2.0 error codes.
constexpr int kParseErrorCode = -32700;
constexpr int kInvalidRequestCode = -32600;
constexpr int kMethodNotFoundCode = -32601;

// One open document. `path` is empty for URIs the server cannot map to a file
// (the document then only ever publishes an empty diagnostics array).
struct Document {
    std::string uri;
    std::string path;
    std::string text;
    bool dirty = false;
};

const std::string* stringField(const Json& object, std::string_view key) {
    const Json* value = object.find(key);
    return value != nullptr ? value->asString() : nullptr;
}

// Last-resort diagnostic when the checker itself fails; the CLI glue already
// guards its own work, so reaching this means a bug worth surfacing.
RawDiagnostic internalError(const std::string& what) {
    RawDiagnostic raw;
    raw.code = diagcode::kInternalError;
    raw.severity = 1;
    raw.message = "internal error while checking: " + what;
    return raw;
}

class Server {
public:
    explicit Server(const CheckFn& check) : check_(check) {}

    // Reads and handles frames until `exit` or end of input.
    int run();

private:
    void handleBody(const std::string& body);
    void handleMessage(const Json& message);
    void handleRequest(const std::string& method, const Json& id);
    void handleNotification(const Json& message, const std::string& method);

    void openDocument(const Json& params);
    void changeDocument(const Json& params);
    void saveDocument(const Json& params);
    void closeDocument(const Json& params);
    Document* documentFor(const Json& params);
    Document* findDocument(const std::string& uri);

    void drainBurst();
    void runPendingChecks();
    void checkDocument(Document& document);
    std::vector<RawDiagnostic> runCheck(const Document& document) const;

    void publish(const std::string& uri,
                 const std::vector<RawDiagnostic>& diagnostics,
                 const std::string& text);
    void publishEmpty(const std::string& uri);
    void respond(const Json& id, Json result);
    void respondError(const Json& id, int code, const std::string& message);
    void emit(const Json& message);

    const CheckFn& check_;
    std::map<std::string, Document> documents_;
    bool shutdownRequested_ = false;
    bool stop_ = false;
    int exitCode_ = 0;
};

int Server::run() {
    setBinaryStdio();
    while (!stop_) {
        std::string body;
        std::string err;
        if (!readFrame(stdin, body, err)) {
            if (err != kEofReason) {
                std::fprintf(stderr, "cp3c lsp: %s\n", err.c_str());
                exitCode_ = 2;
            }
            break;
        }
        handleBody(body);
        if (stop_) break;
        drainBurst();
        runPendingChecks();
    }
    return exitCode_;
}

void Server::handleBody(const std::string& body) {
    Json message;
    std::string err;
    if (!parseJson(body, message, err)) {
        respondError(jsonNull(), kParseErrorCode, "parse error: " + err);
        return;
    }
    handleMessage(message);
}

void Server::handleMessage(const Json& message) {
    if (!message.isObject()) {
        respondError(jsonNull(), kInvalidRequestCode, "invalid request");
        return;
    }
    const Json* method = message.find("method");
    const std::string* name = method != nullptr ? method->asString() : nullptr;
    const Json* id = message.find("id");
    if (name == nullptr) {
        if (id != nullptr) respondError(*id, kInvalidRequestCode, "invalid request");
        return;
    }
    if (id != nullptr) {
        handleRequest(*name, *id);
    } else {
        handleNotification(message, *name);
    }
}

void Server::handleRequest(const std::string& method, const Json& id) {
    if (method == "initialize") {
        respond(id, initializeResult());
        return;
    }
    if (method == "shutdown") {
        shutdownRequested_ = true;
        respond(id, jsonNull());
        return;
    }
    respondError(id, kMethodNotFoundCode, "method not found: " + method);
}

void Server::handleNotification(const Json& message, const std::string& method) {
    if (method == "exit") {
        // LSP 3.17: exit after shutdown is a success, exit without one is not.
        exitCode_ = shutdownRequested_ ? 0 : 1;
        stop_ = true;
        return;
    }
    if (method == "initialized") return;  // nothing to do (no dynamic capabilities)
    const Json* params = message.find("params");
    if (params == nullptr || !params->isObject()) return;
    if (method == "textDocument/didOpen") openDocument(*params);
    else if (method == "textDocument/didChange") changeDocument(*params);
    else if (method == "textDocument/didSave") saveDocument(*params);
    else if (method == "textDocument/didClose") closeDocument(*params);
    // Unknown notifications are ignored (PKG-CONTRACTS sec.2).
}

Document* Server::findDocument(const std::string& uri) {
    const auto it = documents_.find(uri);
    return it == documents_.end() ? nullptr : &it->second;
}

Document* Server::documentFor(const Json& params) {
    const Json* marker = params.find("textDocument");
    if (marker == nullptr) return nullptr;
    const std::string* uri = stringField(*marker, "uri");
    return uri != nullptr ? findDocument(*uri) : nullptr;
}

void Server::openDocument(const Json& params) {
    const Json* marker = params.find("textDocument");
    if (marker == nullptr) return;
    const std::string* uri = stringField(*marker, "uri");
    if (uri == nullptr) return;
    Document opened;
    opened.uri = *uri;
    const std::string* text = stringField(*marker, "text");
    if (text != nullptr) opened.text = *text;
    std::string path;
    if (uriToPath(opened.uri, path)) opened.path = path;
    opened.dirty = true;
    documents_[opened.uri] = std::move(opened);
}

void Server::changeDocument(const Json& params) {
    Document* document = documentFor(params);
    if (document == nullptr) return;
    // Full sync (textDocumentSync: 1): every change carries the whole text,
    // so the LAST entry is the current revision.
    const Json* changes = params.find("contentChanges");
    if (changes != nullptr && changes->isArray()) {
        for (const Json& change : changes->items) {
            if (!change.isObject()) continue;
            const std::string* text = stringField(change, "text");
            if (text != nullptr) document->text = *text;
        }
    }
    document->dirty = true;
}

void Server::saveDocument(const Json& params) {
    Document* document = documentFor(params);
    if (document == nullptr) return;
    const std::string* text = stringField(params, "text");
    if (text != nullptr) document->text = *text;
    document->dirty = true;
}

void Server::closeDocument(const Json& params) {
    const Json* marker = params.find("textDocument");
    const std::string* uri =
        marker != nullptr ? stringField(*marker, "uri") : nullptr;
    if (uri == nullptr) return;
    documents_.erase(*uri);
    publishEmpty(*uri);
}

void Server::drainBurst() {
    const auto deadline = std::chrono::steady_clock::now() + kCoalesceWindow;
    while (!stop_ && std::chrono::steady_clock::now() < deadline &&
           stdinHasData()) {
        std::string body;
        std::string err;
        if (!readFrame(stdin, body, err)) {
            if (err != kEofReason) {
                std::fprintf(stderr, "cp3c lsp: %s\n", err.c_str());
                exitCode_ = 2;
            }
            stop_ = true;
            return;
        }
        handleBody(body);
    }
}

void Server::runPendingChecks() {
    for (auto& entry : documents_) {
        if (stop_) return;
        Document& document = entry.second;
        if (!document.dirty) continue;
        document.dirty = false;
        checkDocument(document);
    }
}

std::vector<RawDiagnostic> Server::runCheck(const Document& document) const {
    std::vector<RawDiagnostic> diagnostics;
    if (document.path.empty() || !check_) return diagnostics;
    try {
        diagnostics = check_(document.path, document.text);
    } catch (const std::exception& error) {
        diagnostics.clear();
        diagnostics.push_back(internalError(error.what()));
    } catch (...) {
        diagnostics.clear();
        diagnostics.push_back(internalError("unknown exception"));
    }
    return diagnostics;
}

void Server::checkDocument(Document& document) {
    const std::vector<RawDiagnostic> diagnostics = runCheck(document);
    if (stop_) return;
    publish(document.uri, diagnostics, document.text);
}

void Server::publish(const std::string& uri,
                     const std::vector<RawDiagnostic>& diagnostics,
                     const std::string& text) {
    Json message = jsonObject();
    message.set("jsonrpc", jsonString("2.0"));
    message.set("method", jsonString("textDocument/publishDiagnostics"));
    message.set("params", publishDiagnosticsParams(uri, diagnostics, text));
    emit(message);
}

void Server::publishEmpty(const std::string& uri) {
    publish(uri, std::vector<RawDiagnostic>(), std::string());
}

void Server::respond(const Json& id, Json result) {
    emit(responseMessage(id, std::move(result)));
}

void Server::respondError(const Json& id, int code, const std::string& message) {
    emit(errorMessage(id, code, message));
}

void Server::emit(const Json& message) {
    if (!writeFrame(stdout, toJsonText(message))) {
        // stdout is gone: there is nobody left to serve.
        stop_ = true;
        exitCode_ = 2;
    }
}

}  // namespace

int runStdioServer(const CheckFn& check) {
    Server server(check);
    return server.run();
}

}  // namespace cp3::lsp
