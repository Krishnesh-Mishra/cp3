// =============================================================================
// cp3/source.hpp — source text, positions, spans, and the SourceManager.
//
// FROZEN INTERFACE (Beta One). See ARCHITECTURE.md.
//
// Conventions:
//   * offsets are 0-based BYTE offsets into SourceFile::text (cp3 strings are
//     UTF-8 — see docs/language-spec/09-arrays-strings.md — so "column" is
//     measured in bytes, documented rather than silently assumed).
//   * line and column are 1-based, matching every editor and the diagnostics
//     examples in docs/cli-tooling/02-error-diagnostics-source-mapping.md.
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cp3 {

struct SourceFile;

// A single position inside one source file.
struct SourcePos {
    std::uint32_t offset = 0;  // 0-based byte offset
    std::uint32_t line = 1;    // 1-based
    std::uint32_t column = 1;  // 1-based, in bytes

    friend bool operator==(const SourcePos& a, const SourcePos& b) {
        return a.offset == b.offset && a.line == b.line && a.column == b.column;
    }
    friend bool operator!=(const SourcePos& a, const SourcePos& b) { return !(a == b); }
};

// A half-open [begin, end) range inside one source file. A default-constructed
// span is "invalid" (no file); helpers treat invalid spans as "unknown
// location" rather than crashing.
struct SourceSpan {
    const SourceFile* file = nullptr;
    SourcePos begin{};
    SourcePos end{};

    bool valid() const noexcept { return file != nullptr; }
    std::uint32_t length() const noexcept {
        return end.offset >= begin.offset ? end.offset - begin.offset : 0;
    }
    // The smallest span covering both. Invalid operands are ignored.
    static SourceSpan merge(const SourceSpan& a, const SourceSpan& b);

    friend bool operator==(const SourceSpan& a, const SourceSpan& b) {
        return a.file == b.file && a.begin == b.begin && a.end == b.end;
    }
};

// One loaded source file. Owned by SourceManager; stable address for the
// lifetime of the manager.
struct SourceFile {
    std::uint32_t id = 0;             // index in its SourceManager
    std::string path;                 // as provided (for diagnostics)
    std::string canonicalPath;        // absolute/normalized (for the module graph)
    std::string text;                 // full UTF-8 contents
    std::vector<std::uint32_t> lineStarts;  // byte offset of the start of each line

    static std::vector<std::uint32_t> computeLineStarts(std::string_view text);

    // Convert a 0-based byte offset into a 1-based line/column position.
    SourcePos positionAt(std::uint32_t offset) const;

    // Raw bytes covered by a span in *this* file (empty if span is elsewhere).
    std::string_view slice(const SourceSpan& span) const;

    // The 1-based line's text, without the trailing newline.
    std::string_view lineText(std::uint32_t oneBasedLine) const;
};

// Owns every SourceFile for a compilation. All SourceFile pointers handed out
// remain valid until the manager is destroyed.
class SourceManager {
public:
    // Adds a file; `path` is what diagnostics show, `text` is the contents.
    // Returns a stable pointer. Re-adding the same canonicalPath returns the
    // existing file.
    SourceFile* addFile(std::string path, std::string text);

    // Looks up a previously-added file by canonical path, or nullptr.
    SourceFile* find(const std::string& canonicalPath) const;

    SourceFile* get(std::uint32_t id) const;

    const std::vector<std::unique_ptr<SourceFile>>& files() const noexcept { return files_; }

    // Convenience: a span covering [beginOffset, endOffset) of `file`.
    static SourceSpan makeSpan(const SourceFile* file,
                               std::uint32_t beginOffset,
                               std::uint32_t endOffset);

    // A singleton SourceManager for synthetic spans (generated code, tests).
    static SourceManager& synthetic();

private:
    std::vector<std::unique_ptr<SourceFile>> files_;
};

}  // namespace cp3
