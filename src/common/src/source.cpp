#include "cp3/source.hpp"

#include <algorithm>

namespace cp3 {

std::vector<std::uint32_t> SourceFile::computeLineStarts(std::string_view text) {
    std::vector<std::uint32_t> starts;
    starts.push_back(0);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            starts.push_back(static_cast<std::uint32_t>(i + 1));
        }
    }
    return starts;
}

SourcePos SourceFile::positionAt(std::uint32_t offset) const {
    SourcePos pos;
    pos.offset = offset;
    if (lineStarts.empty()) {
        return pos;
    }
    // Greatest line start <= offset.
    auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(), offset);
    std::size_t index = (it == lineStarts.begin()) ? 0
                                                    : static_cast<std::size_t>(it - lineStarts.begin() - 1);
    pos.line = static_cast<std::uint32_t>(index + 1);
    pos.column = offset - lineStarts[index] + 1;
    return pos;
}

std::string_view SourceFile::slice(const SourceSpan& span) const {
    if (span.file != this || span.begin.offset > text.size()) {
        return {};
    }
    std::uint32_t begin = span.begin.offset;
    std::uint32_t end = std::min<std::uint32_t>(span.end.offset, static_cast<std::uint32_t>(text.size()));
    if (end < begin) {
        return {};
    }
    return std::string_view(text).substr(begin, end - begin);
}

std::string_view SourceFile::lineText(std::uint32_t oneBasedLine) const {
    if (oneBasedLine == 0 || oneBasedLine > lineStarts.size()) {
        return {};
    }
    std::size_t idx = oneBasedLine - 1;
    std::uint32_t begin = lineStarts[idx];
    std::uint32_t end = (idx + 1 < lineStarts.size())
                            ? lineStarts[idx + 1]
                            : static_cast<std::uint32_t>(text.size());
    // Drop the newline (and a preceding CR, for CRLF files).
    while (end > begin && (text[end - 1] == '\n' || text[end - 1] == '\r')) {
        --end;
    }
    return std::string_view(text).substr(begin, end - begin);
}

SourceSpan SourceSpan::merge(const SourceSpan& a, const SourceSpan& b) {
    if (!a.valid()) return b;
    if (!b.valid()) return a;
    if (a.file != b.file) return a;
    SourceSpan out;
    out.file = a.file;
    std::uint32_t beginOffset = std::min(a.begin.offset, b.begin.offset);
    std::uint32_t endOffset = std::max(a.end.offset, b.end.offset);
    out.begin = a.file->positionAt(beginOffset);
    out.end = a.file->positionAt(endOffset);
    return out;
}

// -----------------------------------------------------------------------------
// SourceManager
// -----------------------------------------------------------------------------
static std::string canonicalize(const std::string& path) {
    std::string out = path;
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

SourceFile* SourceManager::addFile(std::string path, std::string text) {
    std::string canonical = canonicalize(path);
    if (SourceFile* existing = find(canonical)) {
        return existing;
    }
    auto file = std::make_unique<SourceFile>();
    file->id = static_cast<std::uint32_t>(files_.size());
    file->path = std::move(path);
    file->canonicalPath = std::move(canonical);
    file->text = std::move(text);
    file->lineStarts = SourceFile::computeLineStarts(file->text);
    SourceFile* raw = file.get();
    files_.push_back(std::move(file));
    return raw;
}

SourceFile* SourceManager::find(const std::string& canonicalPath) const {
    std::string canonical = canonicalize(canonicalPath);
    for (const auto& f : files_) {
        if (f->canonicalPath == canonical) {
            return f.get();
        }
    }
    return nullptr;
}

SourceFile* SourceManager::get(std::uint32_t id) const {
    if (id >= files_.size()) {
        return nullptr;
    }
    return files_[id].get();
}

SourceSpan SourceManager::makeSpan(const SourceFile* file,
                                   std::uint32_t beginOffset,
                                   std::uint32_t endOffset) {
    SourceSpan span;
    span.file = file;
    if (file) {
        span.begin = file->positionAt(beginOffset);
        span.end = file->positionAt(endOffset);
    }
    return span;
}

SourceManager& SourceManager::synthetic() {
    static SourceManager instance;
    return instance;
}

}  // namespace cp3
