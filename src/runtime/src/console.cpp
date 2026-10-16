// =============================================================================
// cp3/src/console.cpp — D49 rendering and the console output surface.
// =============================================================================
#include "cp3/console.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

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

void writeLine(std::ostream& stream, const std::string& text) {
    stream << text << '\n';
    stream.flush();
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

void writeLineStdout(const std::string& text) { writeLine(std::cout, text); }
void writeLineStderr(const std::string& text) { writeLine(std::cerr, text); }

void clear() {
    // ANSI clear-screen + cursor home (spec: console.clear()).
    std::cout << "\x1b[2J\x1b[H";
    std::cout.flush();
}

}  // namespace console

}  // namespace cp3::runtime
