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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

#include "detail/framing.hpp"
#include "detail/lsp_intel.hpp"
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

// A borrowed view of a document for the intel providers.
DocumentView viewOf(const Document& document) {
    DocumentView view;
    view.uri = &document.uri;
    view.path = &document.path;
    view.text = &document.text;
    return view;
}

// The document and position of a position-based request; `document` is null
// when the request names no open document (the caller answers empty).
struct RequestTarget {
    Document* document = nullptr;
    std::uint32_t line = 0;       // 0-based
    std::uint32_t character = 0;  // 0-based UTF-16 code units
};

const std::string* stringField(const Json& object, std::string_view key) {
    const Json* value = object.find(key);
    return value != nullptr ? value->asString() : nullptr;
}

// A lowercased copy for case-insensitive substring matching.
std::string lowerCopy(std::string_view text) {
    std::string result(text);
    for (char& ch : result) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return result;
}

// Fuzzy match in the style of the platform's symbol search: every query
// character must appear, in order, in the candidate (case-insensitive),
// matching the open-document path.
bool fuzzySubsequence(std::string_view haystack, std::string_view needle) {
    std::size_t cursor = 0;
    for (char ch : needle) {
        const std::size_t found = haystack.find(ch, cursor);
        if (found == std::string_view::npos) return false;
        cursor = found + 1;
    }
    return true;
}

// Normalized key for path comparison (case-insensitive, slash-separated).
std::string normalizePathKey(const std::string& path) {
    std::string key(path);
    for (char& ch : key) {
        if (ch == '\\') ch = '/';
        else if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return key;
}

std::uint32_t numberField(const Json& object, std::string_view key,
                          std::uint32_t fallback) {
    const Json* value = object.find(key);
    if (value == nullptr || !value->isNumber()) return fallback;
    const long long parsed = std::strtoll(value->number.c_str(), nullptr, 10);
    if (parsed < 0) return 0;
    if (parsed > 0xFFFFFFFFLL) return 0xFFFFFFFFu;
    return static_cast<std::uint32_t>(parsed);
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
    Server(const CheckFn& check, const IntelHooks* intel)
        : check_(check), intel_(intel) {}

    // Reads and handles frames until `exit` or end of input.
    int run();

private:
    void handleBody(const std::string& body);
    void handleMessage(const Json& message);
    void handleRequest(const std::string& method, const Json& id,
                       const Json* params);
    bool handleIntelRequest(const std::string& method, const Json& id,
                            const Json* params);
    bool handleIntelPositionRequest(const std::string& method, const Json& id,
                                    const Json* params);
    bool handleIntelDocumentRequest(const std::string& method, const Json& id,
                                    const Json* params);
    void handleNotification(const Json& message, const std::string& method);

    void openDocument(const Json& params);
    void changeDocument(const Json& params);
    void saveDocument(const Json& params);
    void closeDocument(const Json& params);
    Document* documentFor(const Json& params);
    Document* findDocument(const std::string& uri);
    RequestTarget requestTarget(const Json* params);

    void drainBurst();
    void runPendingChecks();
    void checkDocument(Document& document);
    std::vector<RawDiagnostic> runCheck(const Document& document) const;
    void scanWorkspaceSymbols(const std::string& query, Json& out);
    void collectWorkspaceFiles(const std::string& dir, std::vector<std::string>& files,
                               int depth) const;

    struct WorkspaceScanEntry {
        std::filesystem::file_time_type mtime{};
        std::uintmax_t size = 0;
        bool ok = false;
        std::string uri;
        std::vector<SymbolEntry> entries;
        std::string text;
    };

    void publish(const std::string& uri,
                 const std::vector<RawDiagnostic>& diagnostics,
                 const std::string& text);
    void publishEmpty(const std::string& uri);
    void respond(const Json& id, Json result);
    void respondError(const Json& id, int code, const std::string& message);
    void emit(const Json& message);

    const CheckFn& check_;
    const IntelHooks* intel_ = nullptr;
    std::map<std::string, Document> documents_;
    bool shutdownRequested_ = false;
    bool stop_ = false;
    int exitCode_ = 0;
    // The workspace root from `initialize` (rootUri/rootPath), empty when the
    // client sent neither; drives the workspace/symbol disk scan.
    std::string rootPath_;
    // mtime/size-keyed cache of scraped workspace files. The walk happens on
    // every query (it is a linear directory scan with cached reads), so a
    // file written a moment ago is always visible - a stale-serving TTL
    // would hide just-written fixtures from exactly the first query.
    std::unordered_map<std::string, WorkspaceScanEntry> workspaceScanCache_;
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
        handleRequest(*name, *id, message.find("params"));
    } else {
        handleNotification(message, *name);
    }
}

void Server::handleRequest(const std::string& method, const Json& id,
                           const Json* params) {
    if (method == "initialize") {
        // The workspace root is what workspace/symbol scans when a file has
        // no open document; absent root means only open documents are searched.
        if (params != nullptr && params->isObject()) {
            std::string root;
            if (const std::string* uri = stringField(*params, "rootUri");
                uri != nullptr && !uri->empty()) {
                if (!uriToPath(*uri, root)) root.clear();
            } else if (const std::string* path = stringField(*params, "rootPath");
                       path != nullptr && !path->empty()) {
                root = *path;
            }
            rootPath_ = std::move(root);
        }
        respond(id, initializeResult());
        return;
    }
    if (method == "shutdown") {
        shutdownRequested_ = true;
        respond(id, jsonNull());
        return;
    }
    if (intel_ != nullptr && handleIntelRequest(method, id, params)) return;
    respondError(id, kMethodNotFoundCode, "method not found: " + method);
}

bool Server::handleIntelRequest(const std::string& method, const Json& id,
                                const Json* params) {
    return handleIntelPositionRequest(method, id, params) ||
           handleIntelDocumentRequest(method, id, params);
}

// completion/hover/definition/signatureHelp: textDocument + position.
bool Server::handleIntelPositionRequest(const std::string& method,
                                        const Json& id, const Json* params) {
    const bool listAnswer = method == "textDocument/completion" ||
                            method == "textDocument/definition";
    if (method != "textDocument/completion" && method != "textDocument/hover" &&
        method != "textDocument/definition" &&
        method != "textDocument/signatureHelp") {
        return false;
    }
    const RequestTarget target = requestTarget(params);
    if (target.document == nullptr) {
        respond(id, listAnswer ? jsonArray() : jsonNull());
        return true;
    }
    const DocumentView view = viewOf(*target.document);
    if (method == "textDocument/completion") {
        respond(id, completionResult(view, target.line, target.character, *intel_));
    } else if (method == "textDocument/hover") {
        respond(id, hoverResult(view, target.line, target.character, *intel_));
    } else if (method == "textDocument/definition") {
        respond(id, definitionResult(view, target.line, target.character, *intel_));
    } else {
        respond(id, signatureHelpResult(view, target.line, target.character, *intel_));
    }
    return true;
}

// documentSymbol/formatting/workspaceSymbol: no position required.
bool Server::handleIntelDocumentRequest(const std::string& method,
                                        const Json& id, const Json* params) {
    if (method == "textDocument/documentSymbol") {
        const RequestTarget target = requestTarget(params);
        if (target.document == nullptr) respond(id, jsonArray());
        else respond(id, documentSymbolResult(viewOf(*target.document), *intel_));
        return true;
    }
    if (method == "textDocument/formatting") {
        const RequestTarget target = requestTarget(params);
        if (target.document == nullptr) respond(id, jsonArray());
        else respond(id, formattingResult(viewOf(*target.document), *intel_));
        return true;
    }
    if (method == "workspace/symbol") {
        const std::string* query = nullptr;
        if (params != nullptr && params->isObject()) {
            const Json* value = params->find("query");
            if (value != nullptr) query = value->asString();
        }
        std::vector<DocumentView> views;
        for (const auto& entry : documents_) views.push_back(viewOf(entry.second));
        Json symbols = workspaceSymbolResult(query != nullptr ? *query : std::string(),
                                             views, *intel_);
        if (!rootPath_.empty()) {
            scanWorkspaceSymbols(query != nullptr ? *query : std::string(), symbols);
        }
        respond(id, std::move(symbols));
        return true;
    }
    if (method == "textDocument/foldingRange") {
        const RequestTarget target = requestTarget(params);
        if (target.document == nullptr) respond(id, jsonArray());
        else respond(id, foldingRangeResult(viewOf(*target.document)));
        return true;
    }
    return false;
}

RequestTarget Server::requestTarget(const Json* params) {
    RequestTarget target;
    if (params == nullptr || !params->isObject()) return target;
    target.document = documentFor(*params);
    const Json* position = params->find("position");
    if (position == nullptr || !position->isObject()) return target;
    target.line = numberField(*position, "line", 0);
    target.character = numberField(*position, "character", 0);
    return target;
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

// Caps for the workspace/symbol disk scan: the walk sees the whole
// workspace (a cap that arbitrarily drops files would hide symbols, which is
// worse than the extra stat), but a per-file size cap keeps memory bounded,
// and a short TTL bounds how often the walk runs. Stale cache entries (paths
// no longer present) are evicted after each walk.
constexpr std::uintmax_t kMaxWorkspaceScanBytes = 1024 * 1024;   // 1 MiB
constexpr std::size_t kMaxWorkspaceSymbolsTotal = 512;
constexpr int kMaxWorkspaceScanDepth = 12;

void Server::scanWorkspaceSymbols(const std::string& query, Json& out) {
    const std::string needle = lowerCopy(query);

    // Open documents were already answered from their (possibly dirty) text.
    std::vector<std::string> openKeys;
    openKeys.reserve(documents_.size());
    for (const auto& entry : documents_) {
        if (!entry.second.path.empty()) {
            openKeys.push_back(normalizePathKey(entry.second.path));
        }
    }
    std::sort(openKeys.begin(), openKeys.end());

    std::vector<std::string> files;
    collectWorkspaceFiles(rootPath_, files, 0);
    if (files.empty()) return;
    std::sort(files.begin(), files.end());

    // Evict cache entries for files that disappeared (renamed/deleted) so the
    // cache tracks the live source set instead of growing forever.
    {
        std::unordered_set<std::string> live;
        live.reserve(files.size());
        for (const std::string& file : files) live.insert(normalizePathKey(file));
        for (auto it = workspaceScanCache_.begin(); it != workspaceScanCache_.end();) {
            if (live.find(it->first) == live.end()) {
                it = workspaceScanCache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (const std::string& file : files) {
        if (out.items.size() >= kMaxWorkspaceSymbolsTotal) break;
        const std::string key = normalizePathKey(file);
        if (std::binary_search(openKeys.begin(), openKeys.end(), key)) continue;
        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(file, ec);
        if (ec) continue;
        const auto size = std::filesystem::file_size(file, ec);
        if (ec || size > kMaxWorkspaceScanBytes) continue;

        auto cached = workspaceScanCache_.find(key);
        const bool fresh = cached != workspaceScanCache_.end() &&
                           cached->second.ok && cached->second.mtime == mtime &&
                           cached->second.size == size;
        if (!fresh) {
            std::ifstream stream(file, std::ios::binary);
            if (!stream) {
                workspaceScanCache_[key] = WorkspaceScanEntry{};
                continue;
            }
            std::string text((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
            WorkspaceScanEntry slot;
            slot.mtime = mtime;
            slot.size = size;
            slot.uri = pathToUri(file);
            slot.entries = scrapeSymbols(text);
            slot.text = std::move(text);
            slot.ok = true;
            workspaceScanCache_[key] = std::move(slot);
            cached = workspaceScanCache_.find(key);
        }
        const WorkspaceScanEntry& entry = cached->second;
        if (!entry.ok) continue;
        for (const SymbolEntry& symbol : entry.entries) {
            if (out.items.size() >= kMaxWorkspaceSymbolsTotal) break;
            if (!fuzzySubsequence(lowerCopy(symbol.name), needle)) continue;
            out.push(symbolInformationFor(entry.text, entry.uri, symbol));
        }
    }
}

void Server::collectWorkspaceFiles(const std::string& dir,
                                   std::vector<std::string>& files,
                                   int depth) const {
    if (depth > kMaxWorkspaceScanDepth) return;
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) return;
    for (const auto& entry : it) {
        const std::string name = entry.path().filename().string();
        std::error_code typeEc;
        if (entry.is_directory(typeEc)) {
            if (typeEc || name.empty() || name.front() == '.') continue;
            if (name == "node_modules" || name == "build" || name == "dist" ||
                name == "third_party" || name == "generated" || name == "out" ||
                name == "target" || name == "vendor") continue;
            collectWorkspaceFiles(entry.path().string(), files, depth + 1);
        } else if (entry.is_regular_file(typeEc)) {
            if (!typeEc && entry.path().extension() == ".cp3") {
                files.push_back(entry.path().string());
            }
        }
    }
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
    Server server(check, nullptr);
    return server.run();
}

int runStdioServer(const CheckFn& check, const IntelHooks& intel) {
    Server server(check, &intel);
    return server.run();
}

}  // namespace cp3::lsp
