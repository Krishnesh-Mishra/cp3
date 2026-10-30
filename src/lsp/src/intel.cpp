// =============================================================================
// src/lsp/src/intel.cpp - query providers (wire shape) from lsp_intel.hpp.
//
// OWNERSHIP: LSP workstream (src/lsp/**). Implements detail/lsp_intel.hpp.
//
// One function per LSP request method. Each converts the request's UTF-16
// position into the compiler's byte coordinates, calls the matching IntelHook
// and packages the answer. Every failure mode (unknown document, missing
// hook, throwing hook) degrades to the empty result for that method: the
// client sees a well-formed no-answer, never a broken frame or a dead server.
// =============================================================================
#include "detail/lsp_intel.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "detail/lsp_text.hpp"

namespace cp3::lsp {
namespace {

// Caps keep answers bounded even for a pathological document; well above any
// real completion list (the semantic engine offers at most a few hundred).
constexpr std::size_t kMaxCompletionItems = 512;
constexpr std::size_t kMaxWorkspaceSymbols = 512;

Json positionJson(const LspPosition& pos) {
    Json object = jsonObject();
    object.set("line", jsonInt(pos.line));
    object.set("character", jsonInt(pos.character));
    return object;
}

bool endsAfterStart(const LspPosition& end, const LspPosition& start) {
    if (end.line != start.line) return end.line > start.line;
    return end.character > start.character;
}

// A range from compiler coordinates in `text`. A missing/degenerate end
// advances one character: clients render a zero-width range poorly.
Json rangeJson(const std::string& text, std::uint32_t line,
               std::uint32_t column, std::uint32_t endLine,
               std::uint32_t endColumn) {
    const LspPosition start = toLspPosition(text, line, column);
    LspPosition end = advanceOne(text, start);
    if (endLine != 0 && endColumn != 0) {
        const LspPosition reported = toLspPosition(text, endLine, endColumn);
        if (endsAfterStart(reported, start)) end = reported;
    }
    Json range = jsonObject();
    range.set("start", positionJson(start));
    range.set("end", positionJson(end));
    return range;
}

// A Markdown documentation block (the LSP `MarkupContent` shape the editor
// renders as formatted text).
Json markdownDoc(const std::string& value) {
    Json doc = jsonObject();
    doc.set("kind", jsonString("markdown"));
    doc.set("value", jsonString(value));
    return doc;
}

Json completionItemJson(const CompletionCandidate& candidate,
                        std::size_t priority) {
    Json item = jsonObject();
    item.set("label", jsonString(candidate.label));
    item.set("kind", jsonInt(candidate.kind));
    // Preserve the compiler's ranking: locals first, then imports and module
    // names, then keywords. Without sortText VS Code alphabetizes the list.
    std::string sortText = std::to_string(priority);
    if (sortText.size() < 8) sortText.insert(0, 8 - sortText.size(), '0');
    item.set("sortText", jsonString(sortText));
    if (!candidate.detail.empty()) {
        item.set("detail", jsonString(candidate.detail));
    }
    if (!candidate.documentation.empty()) {
        item.set("documentation", markdownDoc(candidate.documentation));
    }
    if (candidate.insertTextFormat == 2 && !candidate.insertText.empty()) {
        item.set("insertText", jsonString(candidate.insertText));
        item.set("insertTextFormat", jsonInt(2));
    }
    return item;
}

Json symbolJson(const std::string& text, const SymbolEntry& entry) {
    const LspPosition start = toLspPosition(text, entry.line, entry.column);
    Json item = jsonObject();
    item.set("name", jsonString(entry.name));
    item.set("kind", jsonInt(entry.kind));
    item.set("range",
             rangeJson(text, entry.line, entry.column, entry.endLine, entry.endColumn));
    Json selection = jsonObject();
    selection.set("start", positionJson(start));
    selection.set("end", positionJson(advanceOne(text, start)));
    item.set("selectionRange", std::move(selection));
    return item;
}

Json symbolInformationJson(const std::string& text, const std::string& uri,
                           const SymbolEntry& entry) {
    Json location = jsonObject();
    location.set("uri", jsonString(uri));
    location.set("range",
                 rangeJson(text, entry.line, entry.column, entry.endLine, entry.endColumn));
    Json item = jsonObject();
    item.set("name", jsonString(entry.name));
    item.set("kind", jsonInt(entry.kind));
    item.set("location", std::move(location));
    return item;
}

// A document the providers can answer for: mapped to a file with content.
bool hasTarget(const DocumentView& document) {
    return document.path != nullptr && document.text != nullptr &&
           !document.path->empty();
}

std::string lowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

bool containsAscii(const std::string& haystack, const std::string& needle) {
    return needle.empty() || haystack.find(needle) != std::string::npos;
}

// The declaration's own text when the target file is not the queried one.
std::optional<std::string> fetchTarget(const std::string& path,
                                       const IntelHooks& intel) {
    if (!intel.readFile) return std::nullopt;
    try {
        return intel.readFile(path);
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace

Json completionResult(const DocumentView& document, std::uint32_t line,
                      std::uint32_t character, const IntelHooks& intel) {
    Json items = jsonArray();
    if (hasTarget(document) && intel.completion) {
        std::vector<CompletionCandidate> candidates;
        try {
            const std::uint32_t column =
                toByteColumn(*document.text, line, character);
            candidates =
                intel.completion(*document.path, *document.text, line + 1, column);
        } catch (...) {
            candidates.clear();
        }
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const CompletionCandidate& candidate = candidates[i];
            if (items.items.size() >= kMaxCompletionItems) break;
            items.push(completionItemJson(candidate, i));
        }
    }
    Json result = jsonObject();
    result.set("isIncomplete", jsonBool(false));
    result.set("items", std::move(items));
    return result;
}

Json hoverResult(const DocumentView& document, std::uint32_t line,
                 std::uint32_t character, const IntelHooks& intel) {
    if (!hasTarget(document) || !intel.hover) return jsonNull();
    std::optional<HoverAnswer> answer;
    try {
        const std::uint32_t column = toByteColumn(*document.text, line, character);
        answer = intel.hover(*document.path, *document.text, line + 1, column);
    } catch (...) {
        answer.reset();
    }
    if (!answer || (answer->contents.empty() && answer->documentation.empty())) {
        return jsonNull();
    }
    // The signature (or the inferred type) goes into a code block; the
    // documentation is appended as markdown, so the editor formats the
    // description and the parameter list.
    std::string value;
    if (!answer->contents.empty()) {
        value = "```cp3\n" + answer->contents + "\n```";
    }
    if (!answer->documentation.empty()) {
        if (!value.empty()) value += "\n\n";
        value += answer->documentation;
    }
    Json contents = jsonObject();
    contents.set("kind", jsonString("markdown"));
    contents.set("value", jsonString(value));
    Json result = jsonObject();
    result.set("contents", std::move(contents));
    return result;
}

Json definitionResult(const DocumentView& document, std::uint32_t line,
                      std::uint32_t character, const IntelHooks& intel) {
    Json locations = jsonArray();
    if (!hasTarget(document) || !intel.definition) return locations;
    std::vector<SourceTarget> targets;
    try {
        const std::uint32_t column = toByteColumn(*document.text, line, character);
        targets =
            intel.definition(*document.path, *document.text, line + 1, column);
    } catch (...) {
        targets.clear();
    }
    for (const SourceTarget& target : targets) {
        if (target.path.empty()) continue;
        std::optional<std::string> fetched;
        const std::string* targetText = document.text;
        if (target.path != *document.path) {
            fetched = fetchTarget(target.path, intel);
            if (!fetched) continue;
            targetText = &*fetched;
        }
        Json location = jsonObject();
        location.set("uri", jsonString(pathToUri(target.path)));
        location.set("range", rangeJson(*targetText, target.line, target.column,
                                        target.endLine, target.endColumn));
        locations.push(std::move(location));
    }
    return locations;
}

Json documentSymbolResult(const DocumentView& document, const IntelHooks& intel) {
    Json symbols = jsonArray();
    if (!hasTarget(document) || !intel.documentSymbols) return symbols;
    std::vector<SymbolEntry> entries;
    try {
        entries = intel.documentSymbols(*document.path, *document.text);
    } catch (...) {
        entries.clear();
    }
    for (const SymbolEntry& entry : entries) {
        symbols.push(symbolJson(*document.text, entry));
    }
    return symbols;
}

Json signatureHelpResult(const DocumentView& document, std::uint32_t line,
                         std::uint32_t character, const IntelHooks& intel) {
    if (!hasTarget(document) || !intel.signatureHelp) return jsonNull();
    std::optional<SignatureAnswer> answer;
    try {
        const std::uint32_t column = toByteColumn(*document.text, line, character);
        answer =
            intel.signatureHelp(*document.path, *document.text, line + 1, column);
    } catch (...) {
        answer.reset();
    }
    if (!answer || answer->label.empty()) return jsonNull();
    Json parameters = jsonArray();
    for (std::size_t i = 0; i < answer->parameters.size(); ++i) {
        Json item = jsonObject();
        item.set("label", jsonString(answer->parameters[i]));
        if (i < answer->parameterDocs.size() &&
            !answer->parameterDocs[i].empty()) {
            item.set("documentation", markdownDoc(answer->parameterDocs[i]));
        }
        parameters.push(std::move(item));
    }
    Json signature = jsonObject();
    signature.set("label", jsonString(answer->label));
    if (!answer->documentation.empty()) {
        signature.set("documentation", markdownDoc(answer->documentation));
    }
    signature.set("parameters", std::move(parameters));
    Json signatures = jsonArray();
    signatures.push(std::move(signature));
    Json result = jsonObject();
    result.set("signatures", std::move(signatures));
    result.set("activeSignature", jsonInt(0));
    const bool inRange = answer->activeParameter >= 0 &&
                         static_cast<std::size_t>(answer->activeParameter) <
                             answer->parameters.size();
    result.set("activeParameter", jsonInt(inRange ? answer->activeParameter : 0));
    return result;
}

Json formattingResult(const DocumentView& document, const IntelHooks& intel) {
    Json edits = jsonArray();
    if (!hasTarget(document) || !intel.format) return edits;
    std::optional<std::string> formatted;
    try {
        formatted = intel.format(*document.path, *document.text);
    } catch (...) {
        formatted.reset();
    }
    if (!formatted || *formatted == *document.text) return edits;
    Json range = jsonObject();
    range.set("start", positionJson(LspPosition{0, 0}));
    range.set("end", positionJson(endPosition(*document.text)));
    Json edit = jsonObject();
    edit.set("range", std::move(range));
    edit.set("newText", jsonString(*formatted));
    edits.push(std::move(edit));
    return edits;
}

Json workspaceSymbolResult(const std::string& query,
                           const std::vector<DocumentView>& documents,
                           const IntelHooks& intel) {
    Json symbols = jsonArray();
    if (!intel.documentSymbols) return symbols;
    const std::string needle = lowerAscii(query);
    for (const DocumentView& document : documents) {
        if (!hasTarget(document) || document.uri == nullptr) continue;
        std::vector<SymbolEntry> entries;
        try {
            entries = intel.documentSymbols(*document.path, *document.text);
        } catch (...) {
            entries.clear();
        }
        for (const SymbolEntry& entry : entries) {
            if (symbols.items.size() >= kMaxWorkspaceSymbols) return symbols;
            if (!containsAscii(lowerAscii(entry.name), needle)) continue;
            symbols.push(
                symbolInformationJson(*document.text, *document.uri, entry));
        }
    }
    return symbols;
}

}  // namespace cp3::lsp
