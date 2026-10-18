// =============================================================================
// cp3/std/console.hpp - `console.*` backing: D49 rendering + the output surface.
//
// OWNERSHIP: runtime module. Part of the generated-code ABI.
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   `console.log(...)`   -> `::cp3::runtime::console::log(...)`
//   `console.info(...)`  -> `::cp3::runtime::console::info(...)`
//   `console.warn(...)`  -> `::cp3::runtime::console::warn(...)`   (stderr)
//   `console.error(...)` -> `::cp3::runtime::console::error(...)`  (stderr)
//   `console.clear()`    -> `::cp3::runtime::console::clear()`
//   `console.assert(cond, ...args)`
//                        -> `::cp3::runtime::console::assertCondition(cond, ...args)`
//     (`assert` is a C macro in <cassert>, so the function is named
//      `assertCondition`; it never throws and never exits - spec §04.)
//
//   Arguments are rendered by `::cp3::runtime::formatValue` and joined with a
//   single space, then a newline is written. `.log`/`.info` -> stdout,
//   `.warn`/`.error` -> stderr (STDLIB docs §07).
//
//   For a user-defined class/object, codegen emits an explicit object form:
//       ::cp3::runtime::formatObject({
//           {"a", ::cp3::runtime::formatNested(obj->a)},
//           {"b", ::cp3::runtime::formatNested(obj->b)},
//       })
//   Fields appear in property/insertion order (DECISIONS D49).
//
// -----------------------------------------------------------------------------
// RENDERING RULES
// -----------------------------------------------------------------------------
//   * number -> DECISIONS D9 (ECMAScript Number::toString), exact.
//   * string at top level -> the string itself, unquoted (JS `console.log`).
//   * string *inside* an array/object -> single-quoted, JS-style, with `\n`,
//     `\t`, `\\`, `\'` escaped. (D49 pins `[1, 2, 3]` / `{ a: 1 }` only; the
//     nested-string quotes follow JS, and live in `quoteString` alone so the
//     choice is easy to revisit.)
//   * array -> `[` + elements joined by `, ` + `]` (D49: `[1, 2, 3]`).
//   * object -> `{` + ` key: value` joined by `, ` + ` }` (D49: `{ a: 1 }`);
//     an empty object is `{}`.
//   * `undefined` / `null` -> `undefined` / `null`.
// =============================================================================
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cp3/core/runtime.hpp"

namespace cp3::runtime {

// ---------------------------------------------------------------------------
// Value rendering
// ---------------------------------------------------------------------------

// Single quotes + JS-ish escaping, for a string nested inside an array/object.
std::string quoteString(const std::string& value);

// Top-level rendering (a string is printed raw).
std::string formatValue(double value);
std::string formatValue(const std::string& value);
std::string formatValue(bool value);
std::string formatValue(std::nullptr_t);
std::string formatValue(Undefined);

// Nested rendering (a string is quoted). Generic fallback delegates to
// `formatValue`; generated code may add overloads via ADL if it needs to.
template <class T>
std::string formatNested(const T& value) {
    return formatValue(value);
}
std::string formatNested(const std::string& value);

// Arrays: `[1, 2, 3]`. Elements are rendered with `formatNested`.
template <class T>
std::string formatValue(const std::vector<T>& value) {
    std::string out = "[";
    bool first = true;
    for (const T& element : value) {
        if (!first) {
            out += ", ";
        }
        first = false;
        out += formatNested(element);
    }
    out += "]";
    return out;
}

// Plain objects: `{ a: 1 }`. Keys and pre-rendered values, in order.
std::string formatObject(
    std::initializer_list<std::pair<std::string_view, std::string>> fields);

// ---------------------------------------------------------------------------
// The console object (`cp3::runtime::console`)
// ---------------------------------------------------------------------------
namespace console {

namespace detail {

std::string join(const std::vector<std::string>& parts);

template <class... Ts>
std::string renderAll(const Ts&... args) {
    std::vector<std::string> parts;
    parts.reserve(sizeof...(Ts));
    (parts.push_back(cp3::runtime::formatValue(args)), ...);
    return join(parts);
}

}  // namespace detail

void writeLineStdout(const std::string& text);
void writeLineStderr(const std::string& text);

// `.log` / `.info` -> stdout.
template <class... Ts>
void log(const Ts&... args) {
    writeLineStdout(detail::renderAll(args...));
}
template <class... Ts>
void info(const Ts&... args) {
    writeLineStdout(detail::renderAll(args...));
}

// `.warn` / `.error` -> stderr.
template <class... Ts>
void warn(const Ts&... args) {
    writeLineStderr(detail::renderAll(args...));
}
template <class... Ts>
void error(const Ts&... args) {
    writeLineStderr(detail::renderAll(args...));
}

// `console.assert(cond, ...args)`: writes to stderr only when `cond` is false.
// Never throws and never exits (STDLIB docs §04).
template <class... Ts>
void assertCondition(bool condition, const Ts&... args) {
    if (condition) {
        return;
    }
    const std::string rendered = detail::renderAll(args...);
    if (rendered.empty()) {
        writeLineStderr("Assertion failed");
    } else {
        writeLineStderr("Assertion failed: " + rendered);
    }
}

// `console.clear()` - ANSI clear-screen + home.
void clear();

// NOTE (gap): `console.input(prompt)` and `console.readKey()` return
// `Promise<string>` and must not block the event loop (STDLIB docs §04). They
// need an async stdin/tty source and are not implemented in this wave; see the
// runtime report.

}  // namespace console

}  // namespace cp3::runtime
