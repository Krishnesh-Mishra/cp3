// =============================================================================
// src/std/console/runtime.cpp - D49 rendering, the console output surface, and
// the D76 window show/hide.
// =============================================================================
#include "cp3/std/console.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace cp3::runtime {
namespace {

// JS-style single-quoted string, used for strings *nested* in arrays/objects.
std::string quote(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out += '\'';
    for (char c : value) {
        switch (c) {
            case '\'':
                out += "\\'";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    out += '\'';
    return out;
}

// C stdio, not std::cout/std::cerr, on purpose (same note as print/println in
// core/runtime.cpp): including <iostream> gives every program that imports
// std/console the ios_base::Init static initializer and the full locale facet
// machinery it builds - hundreds of KB of linked code and pure startup cost for
// a program that only prints. With the default sync_with_stdio(true), std::cout
// lands in the same stdout buffer anyway, so these calls have identical
// ordering semantics.
void writeLine(std::FILE* stream, const std::string& text) {
    std::fwrite(text.data(), 1, text.size(), stream);
    std::fputc('\n', stream);
    std::fflush(stream);
}

}  // namespace

std::string quoteString(const std::string& value) { return quote(value); }

std::string formatValue(double value) { return toString(value); }

std::string formatValue(const std::string& value) { return value; }

std::string formatValue(bool value) { return toString(value); }

std::string formatValue(std::nullptr_t value) { return toString(value); }

std::string formatValue(Undefined value) { return toString(value); }

std::string formatNested(const std::string& value) { return quote(value); }

std::string formatObject(
    std::initializer_list<std::pair<std::string_view, std::string>> fields) {
    if (fields.size() == 0) {
        return "{}";
    }
    std::string out = "{ ";
    bool first = true;
    for (const std::pair<std::string_view, std::string>& field : fields) {
        if (!first) {
            out += ", ";
        }
        first = false;
        out += std::string(field.first);
        out += ": ";
        out += field.second;
    }
    out += " }";
    return out;
}

namespace console {
namespace detail {

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    bool first = true;
    for (const std::string& part : parts) {
        if (!first) {
            out += ' ';
        }
        first = false;
        out += part;
    }
    return out;
}

}  // namespace detail

void writeLineStdout(const std::string& text) {
    // Open console groups indent every stdout line by two spaces per level.
    writeLine(stdout, groupIndentText() + text);
}
void writeLineStderr(const std::string& text) { writeLine(stderr, text); }

void clear() {
    // ANSI clear-screen + cursor home (spec: console.clear()).
    std::fputs("\x1b[2J\x1b[H", stdout);
    std::fflush(stdout);
}

// D76: hide/show the process's own console window. Windows only, and a
// documented no-op when the process has no console window (GetConsoleWindow
// returns NULL: piped output, a service). Never throws; the window's contents
// and the event loop are untouched, so printing continues into the hidden
// buffer.
void hide() {
#if defined(_WIN32)
    if (HWND window = GetConsoleWindow()) {
        ShowWindow(window, SW_HIDE);
    }
#endif
}

void show() {
#if defined(_WIN32)
    if (HWND window = GetConsoleWindow()) {
        ShowWindow(window, SW_SHOW);
    }
#endif
}

}  // namespace console

}  // namespace cp3::runtime
