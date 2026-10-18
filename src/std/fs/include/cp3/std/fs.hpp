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
// CROSS-FAMILY SEAMS (round 3 launched before rounds 1 and 2 landed; see
// build/scratch/rounds/WAVE-STD-R3-FS-REPORT.md "PENDING DEPENDENCIES")
// -----------------------------------------------------------------------------
//   * `FileStats.modifiedAt: Date` (docs 04) is ABSENT until `std/date`
//     lands; add `double modifiedAt` (Date) + the field here, then re-verify.
//   * A data chunk is a `std::string` until `std/buffer` lands; `Chunk` and
//     `chunkText` below are the single swap point (`chunk.toString()` lowers
//     onto `chunkText`). Swapping means: `using Chunk = Buffer;` the two
//     `on("data", ...)` signatures pick up Buffer, and codegen's listener
//     context type swaps from `std::string` to the Buffer spelling.
//   * `write(chunk: string | Buffer)`: the runtime keeps ONE overload over the
//     chunk representation, so a Buffer argument compiles as soon as the chunk
//     representation has that conversion (it is a string today).
// =============================================================================
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "cp3/async/promise.hpp"
#include "cp3/core/error.hpp"
#include "cp3/core/value.hpp"

namespace cp3::runtime::fs {

/// Implementation state. Namespace scope (not nested in the public classes) so
/// the members can share it; generated code never names these.
namespace detail {
struct ReadStreamImpl;
struct WriteStreamImpl;
}  // namespace detail

// =============================================================================
// FileStats
// =============================================================================
/// `fs.statSync(path)` / `await fs.stat(path)`. The docs surface is
/// `FileStats { size, isFile, isDirectory, modifiedAt }`; `modifiedAt` is a
/// SEAM (see the header note) and lands with `std/date`.
struct FileStats {
    double size = 0.0;
    bool isFile = false;
    bool isDirectory = false;
};

// =============================================================================
// Chunk representation (the Buffer seam)
// =============================================================================
/// A byte chunk handed to a `"data"` listener. `std/buffer` is in flight, so
/// the representation is the byte string itself; `chunkText` is the one
/// function the generated `chunk.toString()` call lowers onto.
using Chunk = std::string;

/// `chunk.toString()`. The single swap point behind `Chunk`: with a real
/// Buffer this decodes its bytes; today the chunk already IS the text.
inline std::string chunkText(const Chunk& chunk) { return chunk; }

// =============================================================================
// Sync surface (throws `Thrown` on failure)
// =============================================================================
/// `fs.readFileSync(path, encoding?)`. Reads the file as bytes; the encoding
/// parameter is accepted and ignored (utf8 is the only meaningful value, and a
/// Buffer-as-string chunk means no decoding step).
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

    void on(const std::string& event, std::function<void(Chunk)> handler) const;
    void on(const std::string& event, std::function<void()> handler) const;

private:
    std::shared_ptr<detail::ReadStreamImpl> impl_;
};

/// `fs.readFileStream(path, chunkSize?)`; 0 (the default) means 64 KiB.
ReadStream readFileStream(const std::string& path, double chunkSize = 0.0);

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
    void end() const;
    void on(const std::string& event, std::function<void(std::string)> handler) const;
    void on(const std::string& event, std::function<void()> handler) const;

private:
    std::shared_ptr<detail::WriteStreamImpl> impl_;
};

WriteStream writeFileStream(const std::string& path);

}  // namespace cp3::runtime::fs
