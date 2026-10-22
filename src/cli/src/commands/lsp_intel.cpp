// =============================================================================
// src/cli/src/commands/lsp_intel.cpp - the semantic engine's plumbing.
//
// OWNERSHIP: cli module (src/cli/**). Implements detail/lsp_intel.hpp.
//
// This unit owns the parts every query shares: running the document through
// the check stages (loadModuleGraph + analyzeProgram) and the position/span
// helpers. The AST walk lives in lsp_intel_walk.cpp; the query answers live in
// the sibling units (lsp_intel_query / lsp_intel_define / lsp_intel_*) and
// the factory at the bottom assembles them into the IntelHooks the server
// consumes, with one try/catch per hook so a broken document can never take
// the server down.
// =============================================================================
#include "detail/lsp_intel.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cp3/analysis.hpp"
#include "cp3/ast.hpp"
#include "cp3/diagnostic.hpp"
#include "cp3/lexer.hpp"
#include "cp3/lsp.hpp"
#include "cp3/module.hpp"
#include "cp3/source.hpp"

#include "detail/format.hpp"
#include "detail/package_json.hpp"
#include "detail/pipeline.hpp"
#include "detail/util.hpp"

namespace cp3::cli {
namespace {

bool beforePos(std::uint32_t line, std::uint32_t column, const SourcePos& pos) {
    if (line != pos.line) return line < pos.line;
    return column < pos.column;
}

}  // namespace

std::unique_ptr<AnalyzedDocument> analyzeDocument(const std::string& path,
                                                  const std::string& text) {
    auto document = std::make_unique<AnalyzedDocument>();
    document->file = document->sources.addFile(path, text);
    if (document->file == nullptr) return document;
    CollectingDiagnosticSink sink;
    document->graph = loadModuleGraph(document->sources, *document->file, sink);
    if (document->graph != nullptr) {
        document->entry = document->graph->entry();
        if (document->entry != nullptr && document->entry->ast != nullptr) {
            CompileOptions compile;
            std::filesystem::path root;
            if (findPackageRoot(std::filesystem::path(path).parent_path(),
                                root)) {
                applyProjectTypeSettings(root, compile);
            }
            compile.nativeBindings = document->graph->nativeBindings();
            analyzeProgram(*document->entry->ast, document->sema, sink,
                           compile);
        }
    }
    document->diagnostics = sink.diagnostics();
    return document;
}

std::uint32_t offsetOf(const SourceFile& file, std::uint32_t line,
                       std::uint32_t column) {
    const std::uint32_t size = static_cast<std::uint32_t>(file.text.size());
    if (line == 0 || line > file.lineStarts.size()) return size;
    const std::uint32_t start = file.lineStarts[line - 1];
    const std::uint32_t lineEnd = line < file.lineStarts.size()
                                      ? file.lineStarts[line]
                                      : size;
    const std::uint32_t want = column > 0 ? column - 1 : 0;
    const std::uint32_t offset = start + want;
    return offset > lineEnd ? lineEnd : offset;
}

bool spanContains(const SourceSpan& span, const SourceFile& file,
                  std::uint32_t line, std::uint32_t column) {
    if (!span.valid() || span.file != &file) return false;
    if (beforePos(line, column, span.begin)) return false;
    return beforePos(line, column, span.end);
}

const Node* innermostNodeAt(const Program& program, const SourceFile& file,
                            std::uint32_t line, std::uint32_t column) {
    const Node* best = nullptr;
    std::uint32_t bestLength = 0;
    forEachNode(program, [&](const Node& node) {
        if (!spanContains(node.span, file, line, column)) return;
        const std::uint32_t length = node.span.length();
        if (best == nullptr || length < bestLength) {
            best = &node;
            bestLength = length;
        }
    });
    return best;
}

bool analysisReady(const AnalyzedDocument& document) {
    return document.file != nullptr && document.entry != nullptr &&
           document.entry->ast != nullptr;
}

bool isPlaceholderType(const Type& type) {
    if (type.isUnknown() || type.isError()) return true;
    return type.kind == TypeKind::Object && !type.name.empty() &&
           type.name.front() == '$';
}

const SymbolInfo* nearestSymbolAt(const SemaResult& sema, const std::string& name,
                                  const SourceFile& file, std::uint32_t offset) {
    const SymbolInfo* best = nullptr;
    for (const auto& entry : sema.symbols) {
        const SymbolInfo& symbol = entry.second;
        if (symbol.name != name) continue;
        if (!symbol.span.valid() || symbol.span.file != &file) continue;
        if (symbol.span.begin.offset > offset) continue;
        if (best == nullptr || symbol.span.begin.offset > best->span.begin.offset) {
            best = &symbol;
        }
    }
    return best;
}

SourceSpan nameSpanIn(const SourceSpan& span, const std::string& name) {
    if (!span.valid() || name.empty()) return span;
    const std::string_view text = span.file->slice(span);
    const std::size_t at = text.find(name);
    if (at == std::string_view::npos) return span;
    const std::uint32_t begin = span.begin.offset + static_cast<std::uint32_t>(at);
    SourceSpan found;
    found.file = span.file;
    found.begin = span.file->positionAt(begin);
    found.end = span.file->positionAt(begin + static_cast<std::uint32_t>(name.size()));
    return found;
}

std::string keyName(const Expr* key) {
    if (key == nullptr) return std::string();
    if (key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*key).name;
    }
    if (key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*key).value;
    }
    return std::string();
}

std::optional<std::string> formatAt(const std::string& path,
                                    const std::string& text) {
    // Mirror `cmdFmt` exactly: lexical problems are refused, never rewritten.
    SourceManager sources;
    SourceFile* file = sources.addFile(path, text);
    if (file == nullptr) return std::nullopt;
    CollectingDiagnosticSink sink;
    Lexer lexer(*file, sink);
    (void)lexer.lex();
    if (sink.hasErrors()) return std::nullopt;
    return formatDocumentText(text);
}

std::optional<std::string> readFileAt(const std::string& path) {
    std::string text;
    if (!readTextFile(fs::path(path), text)) return std::nullopt;
    return text;
}

lsp::IntelHooks makeIntelHooks() {
    lsp::IntelHooks hooks;
    hooks.completion = [](const std::string& path, const std::string& text,
                          std::uint32_t line, std::uint32_t column) {
        try {
            return completeAt(path, text, line, column);
        } catch (...) {
            return std::vector<lsp::CompletionCandidate>();
        }
    };
    hooks.hover = [](const std::string& path, const std::string& text,
                     std::uint32_t line, std::uint32_t column) {
        try {
            return hoverAt(path, text, line, column);
        } catch (...) {
            return std::optional<lsp::HoverAnswer>();
        }
    };
    hooks.definition = [](const std::string& path, const std::string& text,
                          std::uint32_t line, std::uint32_t column) {
        try {
            return definitionAt(path, text, line, column);
        } catch (...) {
            return std::vector<lsp::SourceTarget>();
        }
    };
    hooks.documentSymbols = [](const std::string& path,
                               const std::string& text) {
        try {
            return symbolsAt(path, text);
        } catch (...) {
            return std::vector<lsp::SymbolEntry>();
        }
    };
    hooks.signatureHelp = [](const std::string& path, const std::string& text,
                             std::uint32_t line, std::uint32_t column) {
        try {
            return signatureAt(path, text, line, column);
        } catch (...) {
            return std::optional<lsp::SignatureAnswer>();
        }
    };
    hooks.format = [](const std::string& path, const std::string& text) {
        try {
            return formatAt(path, text);
        } catch (...) {
            return std::optional<std::string>();
        }
    };
    hooks.readFile = [](const std::string& path) {
        try {
            return readFileAt(path);
        } catch (...) {
            return std::optional<std::string>();
        }
    };
    return hooks;
}

}  // namespace cp3::cli
