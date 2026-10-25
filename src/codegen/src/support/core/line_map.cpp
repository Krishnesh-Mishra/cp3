// =============================================================================
// src/codegen/src/support/line_map.cpp - `#line` source mapping (support).
//
// OWNERSHIP: codegen module (src/codegen/**). Defines Generator::emitLineDirective.
//
// The debug workflow (--debug and the DAP adapter's launch) wants DWARF to
// point at the .cp3 source, not at the generated C++. One directive ahead of
// each node's generated text is enough: the compiler attributes the following
// physical line (and therefore the code lowered from it) to the .cp3 file and
// line, so gdb breakpoints, frames and stepping are .cp3-native.
//
// Emission is gated on Generator::lineMapping_ (enabled for --debug only), so
// the incremental and release outputs stay byte-identical.
// =============================================================================
#include "detail/generator/generator.h"

#include <string>

namespace cp3 {
namespace {

// The directive wants the file name as an s-char-sequence: a quote would end
// it and a backslash would read as an escape. The path is therefore spelled
// with '/' separators (accepted by debuggers on every platform) and embedded
// quotes are escaped.
std::string directivePath(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (const char c : raw) {
        if (c == '\\') {
            out.push_back('/');
        } else if (c == '"') {
            out += "\\\"";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace

void Generator::emitLineDirective(const Node& node, std::string& out) {
    if (!lineMapping_ || !node.span.valid()) return;
    const SourceFile& file = *node.span.file;
    const std::string& path =
        file.canonicalPath.empty() ? file.path : file.canonicalPath;
    if (path.empty() || node.span.begin.line == 0) return;
    out += "#line ";
    out += std::to_string(node.span.begin.line);
    out += " \"";
    out += directivePath(path);
    out += "\"\n";
}

void Generator::emitMappedRaw(const Node& node, const std::string& raw,
                              std::string& out) {
    if (!lineMapping_) {
        out += raw;
        return;
    }
    std::size_t start = 0;
    while (start < raw.size()) {
        const std::size_t newline = raw.find('\n', start);
        const std::size_t end =
            newline == std::string::npos ? raw.size() : newline;
        if (end > start) {
            emitLineDirective(node, out);
            out.append(raw, start, end - start);
        }
        if (newline == std::string::npos) break;
        out.push_back('\n');
        start = newline + 1;
    }
}

}  // namespace cp3
