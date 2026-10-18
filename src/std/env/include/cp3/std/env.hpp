// =============================================================================
// cp3/std/env.hpp - `std/env` (`{ env }` named import): .env loading + typed
// reads over `process.env`.
//
// OWNERSHIP: the std/env family (src/std/env/**). Header-only: the .env parser
// and the typed get/has/load surface are `inline`; the values themselves live
// in the process family's overlay (`envSet`/`envGet`/`envHas`,
// cp3/std/process.hpp), which this header composes (STDLIB docs §04: env is
// "built on top of process").
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT (src/std/env/codegen.cpp)
// -----------------------------------------------------------------------------
//   env.get<T>(key)            -> `::cp3::runtime::env::get<T>(key)`
//   env.get<T>(key, {default}) -> `::cp3::runtime::env::get<T>(key, default)`
//   env.has(key)               -> `::cp3::runtime::env::has(key)`
//   env.load(path)             -> `::cp3::runtime::env::load(path)`
//
// -----------------------------------------------------------------------------
// SEMANTICS
// -----------------------------------------------------------------------------
//   * get<T>(key) without a default THROWS `Thrown` when the key is unset
//     (corpus-pinned: env_get_missing_throws) or when the value cannot be
//     converted to T. get<T>(key, fallback) returns the fallback instead of
//     throwing when the key is unset; a present-but-unconvertible value still
//     throws (silently returning the fallback would hide a config error).
//   * T is one of string / number / boolean (the representable primitives);
//     boolean reads "false", "0" and "" as false, anything else as true.
//   * has(key): true when the overlay has the key (env.load always sets one)
//     or the OS environment value is non-empty.
//   * load(path): throws when the file cannot be opened. The format is the
//     usual .env subset: `KEY=VALUE` lines, blank lines and `#` comments
//     skipped, an optional `export ` prefix, surrounding single/double quotes
//     stripped, and an unquoted value's trailing ` # comment` dropped. A line
//     without `=` is skipped (lenient, dotenv-style). Values are written into
//     process.env through `envSet`; the OS environment itself is never
//     modified (children of process.exec/spawn inherit the OS environment
//     only).
// =============================================================================
#pragma once

#include <cstddef>
#include <fstream>
#include <optional>
#include <string>
#include <type_traits>

#include "cp3/core/error.hpp"
#include "cp3/std/process.hpp"

namespace cp3::runtime::env {

namespace detail {

inline std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

/// Convert one raw environment value to T, or throw when it is not a T.
template <class T>
inline T convert(const std::string& key, const std::string& raw) {
    if constexpr (std::is_same_v<T, std::string>) {
        return raw;
    } else if constexpr (std::is_same_v<T, bool>) {
        return !(raw.empty() || raw == "0" || raw == "false");
    } else if constexpr (std::is_same_v<T, double>) {
        try {
            std::size_t used = 0;
            const double value = std::stod(raw, &used);
            if (used == 0) {
                throw std::invalid_argument("no digits");
            }
            return value;
        } catch (...) {
            throw Thrown::make("env: value of '" + key + "' is not a number");
        }
    } else {
        return T{};
    }
}

/// Strip matching quotes, else drop an unquoted trailing ` # comment`.
inline std::string unquoteValue(const std::string& value) {
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
        value.back() == value.front()) {
        return value.substr(1, value.size() - 2);
    }
    const std::size_t comment = value.find(" #");
    if (comment != std::string::npos) return trim(value.substr(0, comment));
    return value;
}

/// Apply one `KEY=VALUE` line; silently ignores anything else.
inline void applyLine(const std::string& rawLine) {
    std::string line = trim(rawLine);
    if (line.empty() || line.front() == '#') return;
    if (line.rfind("export ", 0) == 0) line = trim(line.substr(7));
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) return;
    const std::string key = trim(line.substr(0, equals));
    if (key.empty()) return;
    process::envSet(key, unquoteValue(trim(line.substr(equals + 1))));
}

}  // namespace detail

/// `env.get<T>(key)`: the value, converted to T; throws when the key is unset
/// (or the value is not a T).
template <class T>
inline T get(const std::string& key) {
    const std::optional<std::string> raw = process::envGet(key);
    if (!raw) throw Thrown::make("env: '" + key + "' is not set");
    return detail::convert<T>(key, *raw);
}

/// `env.get<T>(key, { default: fallback })`: the fallback when the key is
/// unset; a present value still wins and is converted (throwing when it is
/// not a T).
template <class T>
inline T get(const std::string& key, T fallback) {
    const std::optional<std::string> raw = process::envGet(key);
    if (!raw) return fallback;
    return detail::convert<T>(key, *raw);
}

/// `env.has(key)`: overlay key present, or a non-empty OS environment value.
inline bool has(const std::string& key) { return process::envHas(key); }

/// `env.load(path)`: parse a .env-style file into `process.env`; throws when
/// the file cannot be opened.
inline void load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw Thrown::make("env.load: cannot open '" + path + "'");
    }
    std::string line;
    bool first = true;
    while (std::getline(file, line)) {
        if (first) {
            // A UTF-8 BOM would otherwise glue itself onto the first key.
            first = false;
            if (line.rfind("\xEF\xBB\xBF", 0) == 0) line.erase(0, 3);
        }
        detail::applyLine(line);
    }
}

}  // namespace cp3::runtime::env
