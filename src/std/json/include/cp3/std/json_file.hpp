// =============================================================================
// cp3/std/json_file.hpp - the `std/json` file API: parseFile(Sync)/stringifyToFile(Sync).
//
// OWNERSHIP: the std/json family (src/std/json/**). Additive to the generated-code
// ABI (D14): all functions are `inline`, header-only. Included by cp3/std/json.hpp,
// which owns the codegen contract comment.
//
// `JSON.parseFileSync<T>(path)` / `JSON.parseFile<T>(path)`:
//   the file's text is read through `std/fs` (readFileSync / readFile), then
//   `build` is called with the text. The generated `build` is the same code
//   the inline/primitive emitters produce for `JSON.parse<T>` — the same
//   record builder, the same readers, so the same accept/reject rules
//   (extras ignored, missing required throws, duplicate keys throw, keys are
//   case-sensitive). The async form rejects with `json::ParseError` for both
//   failure classes (read error boxed, and the builder's ParseError).
// `JSON.stringifyToFileSync<T>(path, text)` / `JSON.stringifyToFile<T>(path, text)`:
//   the file is overwritten with `text` (the pre-serialized JSON produced by
//   the `JSON.stringify` lowering). A write failure is `json::ParseError`.
// =============================================================================
#pragma once

#include <string>
#include <utility>

#include "cp3/async/promise.hpp"
#include "cp3/std/fs.hpp"
#include "cp3/std/json_parse.hpp"

namespace cp3::runtime::json {

// ---------------------------------------------------------------------------
// Read side
// ---------------------------------------------------------------------------

// Reads `path` as text, wrapping any failure (missing file, unreadable,
// invalid UTF-8) into `json::ParseError` — the std/json file API's read-error
// class (std/fs itself throws `Thrown`).
inline std::string readFileText(const std::string& path) {
    try {
        return ::cp3::runtime::fs::readFileSync(path);
    } catch (const ::cp3::runtime::Thrown& e) {
        throw ParseError(std::string("read error: ") + e.toString());
    } catch (const std::exception& e) {
        throw ParseError(std::string("read error: ") + e.what());
    } catch (...) {
        throw ParseError("read error");
    }
}

// `JSON.parseFileSync<T>(path)`: reads `path` and passes the text to `build`,
// which produces the typed value (or throws `json::ParseError`).
template <class F>
auto parseFileSync(const std::string& path, F build) {
    return build(readFileText(path));
}

// `JSON.parseFile<T>(path)`: the same read on the async fs path; a read
// failure rejects with `json::ParseError` (the fs coroutine rejection,
// re-boxed); a parse/shape failure inside the build rejects the same way
// through the promise machinery's exception boxing.
template <class F>
auto parseFile(const std::string& path, F build) {
    return ::cp3::runtime::fs::readFile(path)
        .catchError([](const ::cp3::runtime::Thrown& reason) -> std::string {
            throw ParseError(std::string("read error: ") + reason.toString());
        })
        .then([buildFn = std::move(build)](const std::string& text) {
            return buildFn(text);
        });
}

// ---------------------------------------------------------------------------
// Write side
// ---------------------------------------------------------------------------

// Writes `text` to `path`, replacing any existing file. Any failure throws
// `json::ParseError` — the std/json file API's write-error class.
inline void stringifyToFileSync(const std::string& path, const std::string& text) {
    try {
        ::cp3::runtime::fs::writeFileSync(path, text);
    } catch (const ::cp3::runtime::Thrown& e) {
        throw ParseError(std::string("write error: ") + e.toString());
    } catch (const std::exception& e) {
        throw ParseError(std::string("write error: ") + e.what());
    } catch (...) {
        throw ParseError("write error");
    }
}

// `JSON.stringifyToFile<T>(path, text)`: `Promise<void>`; a write failure
// rejects with `json::ParseError` (the fs coroutine rejection, re-boxed).
inline auto stringifyToFile(const std::string& path, const std::string& text) {
    return ::cp3::runtime::fs::writeFile(path, text)
        .catchError([](const ::cp3::runtime::Thrown& reason) {
            throw ParseError(std::string("write error: ") + reason.toString());
        });
}

}  // namespace cp3::runtime::json
