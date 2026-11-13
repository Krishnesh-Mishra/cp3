// =============================================================================
// src/lsp/src/protocol.cpp - JSON-RPC message construction (LSP wire shape).
//
// OWNERSHIP: LSP workstream (src/lsp/**). Implements detail/lsp_protocol.hpp.
//
// Diagnostics follow PKG-CONTRACTS sec.2: source "cp3c", severity mapped from
// the compiler's severity, `code` only when the compiler provided one, and the
// range derived from the compiler's 1-based line/column. When only a start
// position is known the end advances by one character, clamped to the line.
// =============================================================================
#include "detail/lsp_protocol.hpp"

#include <initializer_list>

#include "cp3/meta/version.hpp"

#include "detail/lsp_text.hpp"

namespace cp3::lsp {
namespace {

Json positionJson(const LspPosition& pos) {
    Json object = jsonObject();
    object.set("line", jsonInt(pos.line));
    object.set("character", jsonInt(pos.character));
    return object;
}

bool endsBefore(const LspPosition& end, const LspPosition& start) {
    if (end.line != start.line) return end.line < start.line;
    return end.character <= start.character;
}

Json triggerCharactersJson(std::initializer_list<const char*> characters) {
    Json list = jsonArray();
    for (const char* character : characters) {
        list.push(jsonString(character));
    }
    return list;
}

Json completionProviderJson() {
    Json provider = jsonObject();
    provider.set("triggerCharacters", triggerCharactersJson({".", "\"", ":"}));
    provider.set("resolveProvider", jsonBool(false));
    return provider;
}

Json signatureHelpProviderJson() {
    Json provider = jsonObject();
    provider.set("triggerCharacters", triggerCharactersJson({"(", ","}));
    return provider;
}

Json diagnosticJson(const std::string& text, const RawDiagnostic& raw) {
    const LspPosition start = toLspPosition(text, raw.line, raw.column);
    LspPosition end = advanceOne(text, start);
    if (raw.endLine != 0 && raw.endColumn != 0) {
        const LspPosition reported = toLspPosition(text, raw.endLine, raw.endColumn);
        if (!endsBefore(reported, start)) end = reported;
    }
    Json range = jsonObject();
    range.set("start", positionJson(start));
    range.set("end", positionJson(end));
    Json diagnostic = jsonObject();
    diagnostic.set("range", std::move(range));
    diagnostic.set("severity", jsonInt(raw.severity));
    if (!raw.code.empty()) diagnostic.set("code", jsonString(raw.code));
    diagnostic.set("source", jsonString("cp3c"));
    diagnostic.set("message", jsonString(raw.message));
    return diagnostic;
}

}  // namespace

Json initializeResult() {
    Json capabilities = jsonObject();
    capabilities.set("textDocumentSync", jsonInt(1));  // full sync
    // Intel round (M2-LSP-INTEL): the query providers, advertised together
    // with the diagnostics surface. Trigger characters mirror the brief:
    // '.', '"' and ':' for completion, '(' and ',' for signature help.
    capabilities.set("completionProvider", completionProviderJson());
    capabilities.set("hoverProvider", jsonBool(true));
    capabilities.set("definitionProvider", jsonBool(true));
    capabilities.set("documentSymbolProvider", jsonBool(true));
    capabilities.set("workspaceSymbolProvider", jsonBool(true));
    capabilities.set("signatureHelpProvider", signatureHelpProviderJson());
    capabilities.set("documentFormattingProvider", jsonBool(true));
    capabilities.set("foldingRangeProvider", jsonBool(true));
    Json serverInfo = jsonObject();
    serverInfo.set("name", jsonString("cp3-lsp"));
    serverInfo.set("version", jsonString(kVersion));
    Json result = jsonObject();
    result.set("capabilities", std::move(capabilities));
    result.set("serverInfo", std::move(serverInfo));
    return result;
}

Json publishDiagnosticsParams(const std::string& uri,
                              const std::vector<RawDiagnostic>& diagnostics,
                              const std::string& text) {
    Json items = jsonArray();
    for (const RawDiagnostic& raw : diagnostics) {
        items.push(diagnosticJson(text, raw));
    }
    Json params = jsonObject();
    params.set("uri", jsonString(uri));
    params.set("diagnostics", std::move(items));
    return params;
}

Json responseMessage(const Json& id, Json result) {
    Json message = jsonObject();
    message.set("jsonrpc", jsonString("2.0"));
    message.set("id", id);
    message.set("result", std::move(result));
    return message;
}

Json errorMessage(const Json& id, int code, std::string message) {
    Json error = jsonObject();
    error.set("code", jsonInt(code));
    error.set("message", jsonString(std::move(message)));
    Json out = jsonObject();
    out.set("jsonrpc", jsonString("2.0"));
    out.set("id", id);
    out.set("error", std::move(error));
    return out;
}

}  // namespace cp3::lsp
