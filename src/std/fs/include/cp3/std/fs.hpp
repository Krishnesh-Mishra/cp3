// =============================================================================
// cp3/std/fs.hpp - `std/fs` backing surface.
//
// OWNERSHIP: the std/fs family (src/std/fs/**). Header is the generated-code
// contract, so keep it additive like every cp3/std header (D14).
//
// -----------------------------------------------------------------------------
// CODEGEN CONTRACT (docs/standard-library/04 section fs)
// -----------------------------------------------------------------------------
//   import fs from "std/fs";  (default export only)
//   fs.<member>(...)  ->  ::cp3::runtime::fs::<member>(...)
//
//   sync  - readFileSync / writeFileSync / appendFileSync / existsSync /
//           mkdirSync / readdirSync / statSync / unlinkSync
//   async - readFile / writeFile / appendFile / mkdir / readdir / stat /
//           unlink (Promise-valued; a failure rejects the promise)
//   stream- readFileStream(path, chunkSize?) -> ReadStream
//           writeFileStream(path)             -> WriteStream
//
// All `*Sync` functions throw a catchable `Thrown` carrying a plain message
// (spec section 07; no Error hierarchy in M1). Every async function is a
// coroutine wrapping the sync body, so a throw becomes a rejection.
//
// Backend: the C++ standard library only (`std::filesystem` + `std::fstream`,
// no vendored dependency). The implementation TU is selected by the family's
// native row only when a program imports "std/fs" (src/std/fs/native.hpp, D68).
//
// -----------------------------------------------------------------------------
// CROSS-FAMILY NOTES (round 3 launched before std/buffer and std/date landed;
// the marked seams were closed in the finish phase after the master merge)
// -----------------------------------------------------------------------------
//   * `FileStats.modifiedAt` is the real `std/date` value (cp3/std/date.hpp).
//   * A data chunk is the real `std/buffer` `Buffer` (`Chunk` below); codegen
//     lowers `chunk.toString()` onto `::cp3::runtime::buffer::toString`.
//   * `write(chunk: string | Buffer)` is two overloads over the same bytes.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "cp3/async/promise.hpp"
#include "cp3/core/error.hpp"
#include "cp3/core/value.hpp"
#include "cp3/std/buffer.hpp"
#include "cp3/std/date.hpp"

namespace cp3::runtime::fs {

/// Implementation state. Namespace scope (not nested in the public classes) so
/// the members can share it; generated code never names these.
namespace detail {
struct ReadStreamImpl;
struct WriteStreamImpl;
struct FsWatcherImpl;
}  // namespace detail

// =============================================================================
// FileStats
// =============================================================================
/// `fs.statSync(path)` / `await fs.stat(path)`. The docs surface is
/// `FileStats { size, isFile, isDirectory, modifiedAt }` with `modifiedAt` the
/// real std/date value.
struct FileStats {
    // The exact byte count: `u64` in a Rust/C++ project, cast to `number` at
    // the read site in a TypeScript project (src/std/fs/codegen.cpp).
    std::uint64_t size = 0;
    bool isFile = false;
    bool isDirectory = false;
    ::cp3::runtime::date::Date modifiedAt;
};

// =============================================================================
// Chunk representation
// =============================================================================
/// A byte chunk handed to a `"data"` listener: the std/buffer `Buffer`.
using Chunk = ::cp3::runtime::buffer::Buffer;

// =============================================================================
// Sync surface (throws `Thrown` on failure)
// =============================================================================
/// `fs.readFileSync(path, encoding?)`. Reads the file as bytes; the encoding
/// parameter is accepted and ignored (utf8 is the only meaningful value for a
/// cp3 string).
std::string readFileSync(const std::string& path, std::string encoding = "");
void writeFileSync(const std::string& path, const std::string& data);
void appendFileSync(const std::string& path, const std::string& data);
bool existsSync(const std::string& path);
/// `fs.mkdirSync(path, recursive?)`. Non-recursive on an existing directory
/// throws EEXIST (Node semantics); recursive is idempotent.
void mkdirSync(const std::string& path, bool recursive = false);
/// The entry names in `path`, sorted for determinism (docs leave the order
/// unspecified).
Array<std::string> readdirSync(const std::string& path);
FileStats statSync(const std::string& path);
void unlinkSync(const std::string& path);

// =============================================================================
// Async surface (Promise-valued, rejects with `Thrown` on failure)
// =============================================================================
Promise<std::string> readFile(const std::string& path, std::string encoding = "");
Promise<void> writeFile(const std::string& path, const std::string& data);
Promise<void> appendFile(const std::string& path, const std::string& data);
Promise<void> mkdir(const std::string& path, bool recursive = false);
Promise<Array<std::string>> readdir(const std::string& path);
Promise<FileStats> stat(const std::string& path);
Promise<void> unlink(const std::string& path);

// =============================================================================
// ReadStream (`fs.readFileStream`)
// =============================================================================
/// Genuine chunked reads: the pump starts at the first `on(...)` registration
/// and posts one read per microtask, so the event loop must be driven (codegen
/// sets needsEventLoop_ for every stream use). Documented events:
///   "data"  -> chunk: Chunk   (default chunk size 64 KiB)
///   "end"   -> no arguments
///   "error" -> message: string (the rendered `Thrown`; a real Error type is a
///              later wave). With no "error" listener the pump throws from its
///              microtask, which terminates the program like an uncaught event.
class ReadStream {
public:
    /// Internal: created by `readFileStream` only.
    explicit ReadStream(std::shared_ptr<detail::ReadStreamImpl> impl);

    /// `"data"`: the chunk is the std/buffer `Buffer`.
    void on(const std::string& event, std::function<void(Chunk)> handler) const;
    /// `"error"`: the message is the rendered `Thrown` text.
    void on(const std::string& event, std::function<void(std::string)> handler) const;
    /// `"end"`, or a listener that ignores its payload.
    void on(const std::string& event, std::function<void()> handler) const;

private:
    std::shared_ptr<detail::ReadStreamImpl> impl_;
};

/// `fs.readFileStream(path, chunkSize?)`; 0 (the default) means 64 KiB.
/// `chunkSize` is the exact `u64` file-size quantity (cast to `number` at the
/// call site in a TypeScript project, like `FileStats.size`).
ReadStream readFileStream(const std::string& path, std::uint64_t chunkSize = 0);

// =============================================================================
// WriteStream (`fs.writeFileStream`)
// =============================================================================
/// `write(chunk)` appends bytes and `end()` closes the file. The destination
/// is opened (truncated) lazily at the first write/end, so an `"error"`
/// listener registered right after creation still observes an open failure.
/// The only documented event is "error" (message: string); with no listener a
/// failure throws from the call site.
class WriteStream {
public:
    /// Internal: created by `writeFileStream` only.
    explicit WriteStream(std::shared_ptr<detail::WriteStreamImpl> impl);

    void write(const std::string& chunk) const;
    void write(const Chunk& chunk) const;
    void end() const;
    void on(const std::string& event, std::function<void(std::string)> handler) const;
    void on(const std::string& event, std::function<void()> handler) const;

private:
    std::shared_ptr<detail::WriteStreamImpl> impl_;
};

WriteStream writeFileStream(const std::string& path);

// =============================================================================
// FsWatcher (`fs.watch`)
// =============================================================================
/// `fs.watch(path, callback)`: watches `path` (a file or a directory) and
/// invokes `callback(eventType, filename)` on every change. `eventType` is
/// "change" (content/metadata) or "rename" (file created/deleted/moved);
/// `filename` is the entry name relative to a watched directory, or "" when
/// the platform does not report one (always "" for a single-file watch on
/// some platforms). `close()` stops delivery and is idempotent. The watcher
/// keeps the event loop alive (pending work) until closed. Backend: libuv
/// `uv_fs_event` on the shared per-thread loop (src/std/fs/runtime_watch.cpp).
class FsWatcher {
public:
    /// Internal: created by `watch` only.
    explicit FsWatcher(std::shared_ptr<detail::FsWatcherImpl> impl);
    ~FsWatcher();  // closes (idempotent) so a dropped watcher releases its retain

    /// Stops delivering events. Idempotent; safe to call twice.
    void close() const;

private:
    std::shared_ptr<detail::FsWatcherImpl> impl_;
};

/// `fs.watch(path, callback)`; throws `Thrown` when the path cannot be
/// watched (e.g. it does not exist). Event-callback errors during running
/// cannot be reported (there is no "error" listener surface) and are dropped.
FsWatcher watch(const std::string& path,
                std::function<void(std::string eventType, std::string filename)> callback);

}  // namespace cp3::runtime::fs
