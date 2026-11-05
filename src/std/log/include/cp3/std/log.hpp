// =============================================================================
// cp3/std/log.hpp - `Logger` (std/log) backing: level-filtered synchronous
// line logging.
//
// OWNERSHIP: the std/log family (src/std/log/**). Header-only (no vendored
// library, no runtime TU): `fmt` is NOT bundled (ROADMAP §C lists it as the
// ledger mechanism, but nothing is vendored, so formatting is hand-written
// here and no download is needed).
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT
// -----------------------------------------------------------------------------
//   `new Logger()`           -> `::cp3::runtime::log::makeLogger()`
//   `new Logger(name)`       -> `::cp3::runtime::log::makeLogger(name)`
//   `log.level`              -> `->level()`  (the lowercase level name)
//   `log.name`               -> `->name()`
//   `log.setLevel(level)`    -> `->setLevel(level)`
//   `log.debug/info/warn/error(msg, ...args)`
//                            -> `->debug/info/warn/error(msg, args...)`
//   `log.setSink(fn)`        -> `->setSink(fn)` (fn takes the full line)
//   `log.resetSink()`        -> `->resetSink()`
//
// -----------------------------------------------------------------------------
// SEMANTICS (pinned by tests/cases/stdlib/02/log/*)
// -----------------------------------------------------------------------------
//   * One synchronous line per call that passes the filter, of the form
//     `LEVEL name message[ arg...]` (uppercase LEVEL; the name and its
//     separating space are omitted for an unnamed logger). There is no
//     timestamp, no source location and no colour: the line is the whole
//     record, which keeps output easy to diff in tests.
//   * The default level is `info`; `debug` needs `setLevel("debug")`.
//   * Levels are ordered debug < info < warn < error: a call is emitted when
//     its level is >= the configured level. `setLevel` accepts the four
//     names case-insensitively; an unknown name leaves the level unchanged.
//   * All levels write one line to stdout synchronously (flushed) by default;
//     there is no stderr split, no buffering and no background thread.
//   * `setSink(fn)` replaces stdout: `fn` receives each emitted line (no
//     trailing newline). `resetSink()` restores stdout.
//   * Message and extra arguments are rendered by the hand-written
//     `detail::renderValue` family (strings raw, numbers shortest-round-trip
//     via `std::to_chars`, booleans `true`/`false`, null/undefined as
//     `null`/`undefined`) and joined with single spaces, like console.*.
// =============================================================================
#pragma once

#include <charconv>
#include <cstddef>
#include <functional>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "cp3/core/value.hpp"

namespace cp3::runtime::log {

enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3 };

namespace detail {

// The lowercase user spelling of one level (`setLevel`/`level`).
inline const char* levelName(Level level) {
    switch (level) {
        case Level::Debug: return "debug";
        case Level::Info: return "info";
        case Level::Warn: return "warn";
        case Level::Error: return "error";
    }
    return "info";
}

// The uppercase prefix of one emitted line.
inline const char* levelTag(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERROR";
    }
    return "INFO";
}

// Lowercase-compare `input` against the four level names (hand-written;
// no regex, no locale).
inline bool levelFromName(const std::string& input, Level& out) {
    std::string lower;
    lower.reserve(input.size());
    for (char ch : input) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        lower.push_back(ch);
    }
    if (lower == "debug") { out = Level::Debug; return true; }
    if (lower == "info") { out = Level::Info; return true; }
    if (lower == "warn") { out = Level::Warn; return true; }
    if (lower == "error") { out = Level::Error; return true; }
    return false;
}

// Hand-written value rendering (no `fmt`): see the header comment. Strings
// are raw (like JS `console.log`); numbers use the shortest round-trip
// `std::to_chars` spelling; booleans/null/undefined spell as JS.
inline std::string renderValue(const std::string& value) { return value; }
inline std::string renderValue(const char* value) {
    return value != nullptr ? std::string(value) : std::string("null");
}
inline std::string renderValue(bool value) { return value ? "true" : "false"; }
inline std::string renderValue(std::nullptr_t) { return "null"; }
inline std::string renderValue(Undefined) { return "undefined"; }
inline std::string renderValue(double value) {
    char buf[40];
    const auto result = std::to_chars(buf, buf + sizeof(buf), value);
    if (result.ec == std::errc()) return std::string(buf, result.ptr);
    return std::string("NaN");
}
// Integral values (e.g. a native-int webserverion) render as their double
// spelling. Without this overload an `int` argument is ambiguous between
// the `bool` and `double` overloads above.
template <class T>
std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>,
                 std::string>
renderValue(T value) {
    return renderValue(static_cast<double>(value));
}

template <class... Ts>
std::string joinArgs(const Ts&... args) {
    std::vector<std::string> parts;
    parts.reserve(sizeof...(Ts));
    (parts.push_back(renderValue(args)), ...);
    std::string out;
    bool first = true;
    for (const std::string& part : parts) {
        if (!first) out += ' ';
        first = false;
        out += part;
    }
    return out;
}

// A sink may declare `(line: string)` or no parameter (like console.on).
template <class F>
void invokeSink(F& fn, const std::string& line) {
    if constexpr (std::is_invocable_v<F&, const std::string&>) {
        fn(line);
    } else {
        fn();
    }
}

}  // namespace detail

class LoggerData {
public:
    explicit LoggerData(std::string name = std::string()) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }

    // The current level in its user spelling (`level`).
    std::string level() const { return detail::levelName(level_); }

    // Set the minimum emitted level. Unknown names leave it unchanged.
    void setLevel(const std::string& name) {
        Level parsed;
        if (detail::levelFromName(name, parsed)) level_ = parsed;
    }

    // Replace the stdout sink: `fn` receives each emitted line verbatim.
    template <class F>
    void setSink(F fn) {
        sink_ = [handler = std::move(fn)](const std::string& line) mutable {
            detail::invokeSink(handler, line);
        };
    }

    void resetSink() { sink_ = nullptr; }

    // The log methods: `msg` first, trailing arguments rendered after it.
    template <class... Ts>
    void debug(const std::string& msg, const Ts&... args) {
        emit(Level::Debug, msg, args...);
    }
    template <class... Ts>
    void info(const std::string& msg, const Ts&... args) {
        emit(Level::Info, msg, args...);
    }
    template <class... Ts>
    void warn(const std::string& msg, const Ts&... args) {
        emit(Level::Warn, msg, args...);
    }
    template <class... Ts>
    void error(const std::string& msg, const Ts&... args) {
        emit(Level::Error, msg, args...);
    }

private:
    template <class... Ts>
    void emit(Level level, const std::string& msg, const Ts&... args) {
        if (level < level_) return;
        std::string line = detail::levelTag(level);
        if (!name_.empty()) {
            line += ' ';
            line += name_;
        }
        line += ' ';
        line += msg;
        const std::string tail = detail::joinArgs(args...);
        if (!tail.empty()) {
            line += ' ';
            line += tail;
        }
        if (sink_) {
            sink_(line);
        } else {
            // One synchronous, flushed line per call: no buffering delays.
            std::cout << line << '\n' << std::flush;
        }
    }

    std::string name_;
    Level level_ = Level::Info;
    std::function<void(const std::string&)> sink_;
};

using Logger = ::cp3::runtime::Ref<LoggerData>;

inline Logger makeLogger() { return ::cp3::runtime::makeRef<LoggerData>(); }

inline Logger makeLogger(const std::string& name) {
    return ::cp3::runtime::makeRef<LoggerData>(name);
}

}  // namespace cp3::runtime::log
