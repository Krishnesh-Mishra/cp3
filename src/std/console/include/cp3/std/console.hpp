// =============================================================================
// cp3/std/console.hpp - `console.*` backing: D49 rendering + the output surface.
//
// OWNERSHIP: the std/console family (src/std/console/**). Part of the generated-code ABI.
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
//   `console.input(prompt?)` -> `::cp3::runtime::console::input(prompt)`
//   `console.readKey()`  -> `::cp3::runtime::console::readKey()`
//   `console.on(ev, fn)` -> `::cp3::runtime::console::on(ev, fn)`
//   `console.off(ev, fn)`-> `::cp3::runtime::console::off(ev, fn)`
//   `console.hide()`     -> `::cp3::runtime::console::hide()`
//   `console.show()`     -> `::cp3::runtime::console::show()`
//   `console.time(label?)`   -> `::cp3::runtime::console::time(label)`
//   `console.timeEnd(label?)`-> `::cp3::runtime::console::timeEnd(label)`
//   `console.time(label?)`    -> `::cp3::runtime::console::time(label)`
//   `console.timeEnd(label?)` -> `::cp3::runtime::console::timeEnd(label)`
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

#include <chrono>
#include <cstdio>
#include <chrono>
#include <functional>
#include <iomanip>
#include <initializer_list>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "cp3/core/runtime.hpp"
#include "cp3/async/promise.hpp"  // input()/readKey() -> Promise<std::string>

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

// Group indentation (console.group/groupEnd): each open group adds two spaces
// to every subsequent stdout line. Kept in an inline-function static so all
// translation units share one counter.
inline int& groupDepthStorage() {
    static int depth = 0;
    return depth;
}
inline std::string groupIndentText() {
    return std::string(static_cast<std::size_t>(groupDepthStorage()) * 2, ' ');
}
// `console.group(...args)`: prints the args at the current depth, then opens
// a group; `console.groupEnd()`: closes one (never below zero).
inline void groupStart(const std::vector<std::string>& parts) {
    writeLineStdout(detail::join(parts));
    ++groupDepthStorage();
}
inline void groupEnd() {
    if (groupDepthStorage() > 0) --groupDepthStorage();
}

// `console.count(label?)` / `console.countReset(label?)`: a per-label counter
// printed as `label: N` (default label `default`).
inline std::map<std::string, int>& countStorage() {
    static std::map<std::string, int> counts;
    return counts;
}
inline void count(const std::string& label) {
    const int n = ++countStorage()[label];
    writeLineStdout(label + ": " + std::to_string(n));
}
inline void countReset(const std::string& label) { countStorage().erase(label); }

// `console.time(label?)` / `console.timeEnd(label?)` (v0.2 ledger, ROADMAP
// §C): a per-label monotonic timer on `std::chrono::steady_clock` (the same
// clock as std/time's Stopwatch; never the wall clock). `time` records the
// start instant for the label (default label `default`), restarting an
// existing one; `timeEnd` prints `label: <ms>ms` (milliseconds, three
// decimals) to stdout and removes the label, Node-style. A `timeEnd` for a
// label that was never started prints `Timer "label" does not exist` to
// stderr and prints nothing to stdout.
inline std::map<std::string, std::chrono::steady_clock::time_point>&
timeStorage() {
    static std::map<std::string, std::chrono::steady_clock::time_point>
        timers;
    return timers;
}
inline void time(const std::string& label) {
    timeStorage()[label] = std::chrono::steady_clock::now();
}
inline void timeEnd(const std::string& label) {
    auto& timers = timeStorage();
    const auto it = timers.find(label);
    if (it == timers.end()) {
        writeLineStderr("Timer \"" + label + "\" does not exist");
        return;
    }
    const double ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - it->second)
            .count();
    timers.erase(it);
    std::ostringstream out;
    out << label << ": " << std::fixed << std::setprecision(3) << ms
        << "ms";
    writeLineStdout(out.str());
}

// `console.trace(...args)`: a `Trace:` line (no stack yet in v0.2).
inline void traceParts(const std::vector<std::string>& parts) {
    writeLineStdout("Trace: " + detail::join(parts));
}
// `console.dir(x)`: prints the value like log, without full depth inspection.
inline void dirParts(const std::vector<std::string>& parts) {
    writeLineStdout(detail::join(parts));
}

// The parts form: a call with a spread argument (`console.log(...parts)`)
// collects the rendered arguments first - a spread contributes the array's
// elements - and the runtime joins them with a single space, exactly like the
// variadic form.
inline void logParts(const std::vector<std::string>& parts) {
    writeLineStdout(detail::join(parts));
}
inline void warnParts(const std::vector<std::string>& parts) {
    writeLineStderr(detail::join(parts));
}
inline void errorParts(const std::vector<std::string>& parts) {
    writeLineStderr(detail::join(parts));
}

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

// The parts form of `assert`: the message arguments are collected first (a
// spread contributes the array's elements) and then joined.
inline void assertConditionParts(bool condition,
                                  const std::vector<std::string>& parts) {
    if (condition) {
        return;
    }
    const std::string rendered = detail::join(parts);
    if (rendered.empty()) {
        writeLineStderr("Assertion failed");
    } else {
        writeLineStderr("Assertion failed: " + rendered);
    }
}

// `console.clear()` - ANSI clear-screen + home.
void clear();

// ---------------------------------------------------------------------------
// The async input surface (D71): input / readKey / on / off
// ---------------------------------------------------------------------------
//
// Never a second loop (D71): a detached helper thread reads stdin and posts
// every event through the loop's cross-thread post (`postFromThread`), so
// waiting for a line or key never blocks pending async work. The pump is
// demand-driven: it reads (and the terminal enters raw mode) while at least one
// `input()`, `readKey()` or listener is outstanding, and stops - restoring the
// terminal - when the last one is gone. Lines and keys fan out (D75): a pending
// request and the matching listeners all observe the same event.

// `console.input(prompt?)`: one line from stdin, terminator removed; EOF
// resolves with `""` (never rejects).
Promise<std::string> input(const std::string& prompt = std::string());

// `console.readKey()`: one keypress; named keys ("ArrowUp"/"ArrowDown"/
// "ArrowLeft"/"ArrowRight", "Enter", "Escape", "Tab", "Backspace") or the
// literal character typed. EOF resolves with `""`.
Promise<std::string> readKey();

// `console.on` / `console.off` (D75): console-internal listener bookkeeping,
// no dependency on the round-2 events wave. Events: `"keypress"` (the readKey
// vocabulary), `"line"` (each completed line, terminator removed) and `"close"`
// (once, with `""`, at stdin EOF). A listener is an observation tap: it never
// consumes an event a pending `input()`/`readKey()` also sees.
//
// The listener's *function type* is its identity: `off(event, fn)` removes the
// listeners registered from the same function type (a named function value, or
// the same lambda variable). Two distinct inline arrow functions at different
// source positions are distinct C++ closure types and do not match; that is the
// C++ boundary of the JS function-reference identity model.
namespace detail {

// One process-lifetime address per listener function type F, so `on`/`off` can
// compare the type instead of the (uncomparable) closure object.
template <class F>
struct ListenerTypeTag {
    static const char value;
};
template <class F>
const char ListenerTypeTag<F>::value = 0;

void addListener(const std::string& event,
                 std::function<void(std::string)> listener, const void* typeTag);
void removeListener(const std::string& event, const void* typeTag);

// A listener may declare the documented `(value: string)` parameter or none
// (`close`'s `""` / `keypress` are easy to ignore): call whichever it takes.
template <class F, class V>
void invokeListener(F& listener, V&& value) {
    if constexpr (std::is_invocable_v<F&, V>) {
        listener(std::forward<V>(value));
    } else {
        listener();
    }
}

}  // namespace detail

template <class F>
void on(const std::string& event, F listener) {
    std::function<void(std::string)> fn =
        [handler = std::move(listener)](std::string value) mutable {
            detail::invokeListener(handler, value);
        };
    detail::addListener(event, std::move(fn), &detail::ListenerTypeTag<F>::value);
}

template <class F>
void off(const std::string& event, F listener) {
    static_cast<void>(listener);
    detail::removeListener(event, &detail::ListenerTypeTag<F>::value);
}

// `console.hide()` / `console.show()` (D76): hide / restore the process's own
// console window. Windows: `GetConsoleWindow()` + `ShowWindow`; never throws,
// the event loop keeps running and printing continues into the hidden buffer.
// Everywhere else - and on Windows without a console window (piped output, a
// service) - both are documented no-ops.
void hide();
void show();

}  // namespace console

}  // namespace cp3::runtime
