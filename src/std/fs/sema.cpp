// =============================================================================
// src/std/fs/sema.cpp - `std/fs` surface typing and documentation.
//
// OWNERSHIP: the std/fs family (src/std/fs/**). One per-module check unit; the
// rest of sema never needs to change for this module (std_check.cpp carries the
// namespace dispatch row and forwards here).
//
// The table mirrors docs/standard-library/04 section fs, the pinned signature
// surface the cases exercise:
//   sync  - readFileSync(path, encoding?): string
//           writeFileSync(path, data): void
//           appendFileSync(path, data): void
//           existsSync(path): boolean
//           mkdirSync(path, recursive?): void
//           readdirSync(path): string[]
//           statSync(path): FileStats
//           unlinkSync(path): void
//   async - readFile / writeFile / appendFile / mkdir / readdir / stat / unlink
//           (same parameters, Promise-wrapped results)
//   stream - readFileStream(path, chunkSize?): ReadStream
//            writeFileStream(path): WriteStream
//
// The same table drives member typing, the CP3305 candidate list and the
// editor documentation, so the three can never drift apart. fs is a CLOSED
// family (std_check.cpp isClosedStdNamespace): a member outside the table is
// CP3305 with these names as candidates, never an untyped fallthrough.
//
// The three receiver objects (FileStats / ReadStream / WriteStream) are named
// permissive markers, exactly like std/network's documented objects: the shape
// is open on purpose, so a member the codegen half does not lower yet keeps the
// old permissive behavior instead of cascading. Codegen recognises receivers by
// these marker names, which are shared with src/std/fs/codegen.cpp.
//
// CROSS-FAMILY NOTES (round 3 launched before std/buffer and std/date landed;
// the marked seams closed in the finish phase):
//   * `FileStats.modifiedAt: Date` types as the nominal `Date` class below, so
//     Date methods chain on the read; codegen lowers the field read.
//   * `ReadStream`'s `data` listener parameter is `chunk: Buffer` in the docs,
//     but sema never consults callback parameter types (codegen types the
//     lambda's own parameter name with the nominal `Buffer` class).
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cp3/host/std_integer.hpp"
#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

// The receiver markers (shared with src/std/fs/codegen.cpp).
const char* kFileStats = "$fs:FileStats";
const char* kReadStream = "$fs:ReadStream";
const char* kWriteStream = "$fs:WriteStream";
const char* kFsWatcher = "$fs:FsWatcher";

// -----------------------------------------------------------------------------
// Receiver objects
// -----------------------------------------------------------------------------
//
// A named open object: no shape is registered, so member access on it stays
// permissive (the std/network convention) and codegen can recognise the
// receiver by name. The documented `FileStats` fields are typed by
// `fsReceiverMember` below; every other member keeps the permissive ambient
// fallthrough, exactly like the network receivers.

Type fsStatsObject() { return Type::named(TypeKind::Object, kFileStats); }
Type fsReadStreamObject() { return Type::named(TypeKind::Object, kReadStream); }
Type fsWriteStreamObject() { return Type::named(TypeKind::Object, kWriteStream); }
Type fsWatcherObject() { return Type::named(TypeKind::Object, kFsWatcher); }

// -----------------------------------------------------------------------------
// Member type factories
// -----------------------------------------------------------------------------

Type promiseVoid() { return promiseOf(Type::voidType()); }
Type promiseString() { return promiseOf(Type::string()); }
Type promiseNames() { return promiseOf(Type::array(Type::string())); }
Type promiseStats() { return promiseOf(fsStatsObject()); }

// `(path, encoding?)` - the trailing parameter is optional.
Type readFileParams(Type result) {
    return withOptionalTail(Type::function({Type::string(), Type::string()}, result));
}

// `(path, data)` - both required.
Type writeParams(Type result) {
    return Type::function({Type::string(), Type::string()}, result);
}

Type mkdirParams(Type result) {
    return withOptionalTail(Type::function({Type::string(), Type::boolean()}, result));
}

const MemberRow kFsRows[] = {
    {"readFileSync", [] { return readFileParams(Type::string()); },
     "readFileSync(path: string, encoding?: string): string",
     "Reads the whole file at `path` and returns its contents. Throws a "
     "catchable error when the file cannot be read."},
    {"writeFileSync", [] { return writeParams(Type::voidType()); },
     "writeFileSync(path: string, data: string): void",
     "Writes `data` to the file at `path`, replacing it. Throws a catchable "
     "error when the file cannot be written."},
    {"appendFileSync", [] { return writeParams(Type::voidType()); },
     "appendFileSync(path: string, data: string): void",
     "Appends `data` to the file at `path`."},
    {"existsSync", [] { return Type::function({Type::string()}, Type::boolean()); },
     "existsSync(path: string): boolean",
     "True when a file or a directory exists at `path`."},
    {"mkdirSync", [] { return mkdirParams(Type::voidType()); },
     "mkdirSync(path: string, recursive?: boolean): void",
     "Creates the directory at `path`; with `recursive`, the missing parent "
     "directories are created too."},
    {"readdirSync", [] { return Type::function({Type::string()}, Type::array(Type::string())); },
     "readdirSync(path: string): string[]",
     "Returns the names of the entries of the directory at `path`."},
    {"statSync", [] { return Type::function({Type::string()}, fsStatsObject()); },
     "statSync(path: string): FileStats",
     "Returns the `FileStats` of the file at `path` (its size, its kind and "
     "its modification time)."},
    {"unlinkSync", [] { return Type::function({Type::string()}, Type::voidType()); },
     "unlinkSync(path: string): void",
     "Removes the file at `path`."},
    {"readFile", [] { return readFileParams(promiseString()); },
     "readFile(path: string, encoding?: string): Promise<string>",
     "Reads the whole file at `path` and resolves with its contents. The "
     "promise rejects when the file cannot be read."},
    {"writeFile", [] { return writeParams(promiseVoid()); },
     "writeFile(path: string, data: string): Promise<void>",
     "Writes `data` to the file at `path`, replacing it."},
    {"appendFile", [] { return writeParams(promiseVoid()); },
     "appendFile(path: string, data: string): Promise<void>",
     "Appends `data` to the file at `path`."},
    {"mkdir", [] { return mkdirParams(promiseVoid()); },
     "mkdir(path: string, recursive?: boolean): Promise<void>",
     "Creates the directory at `path`; with `recursive`, the missing parent "
     "directories are created too."},
    {"readdir", [] { return Type::function({Type::string()}, promiseNames()); },
     "readdir(path: string): Promise<string[]>",
     "Resolves with the names of the entries of the directory at `path`."},
    {"stat", [] { return Type::function({Type::string()}, promiseStats()); },
     "stat(path: string): Promise<FileStats>",
     "Resolves with the `FileStats` of the file at `path`."},
    {"unlink", [] { return Type::function({Type::string()}, promiseVoid()); },
     "unlink(path: string): Promise<void>",
     "Removes the file at `path`."},
    {"readFileStream",
     [] { return withOptionalTail(Type::function({Type::string(), Type::number()},
                                                 fsReadStreamObject())); },
     "readFileStream(path: string, chunkSize?: number): ReadStream",
     "Opens the file at `path` for streaming reads; with `chunkSize`, that is "
     "the size of the chunks the `data` event delivers."},
    {"writeFileStream",
     [] { return Type::function({Type::string()}, fsWriteStreamObject()); },
     "writeFileStream(path: string): WriteStream",
     "Opens the file at `path` for streaming writes."},
    // `fs.watch(path, callback)`: the callback takes (eventType, filename);
    // both are strings. The FsWatcher receiver's `close()` is typed by
    // `fsReceiverMember` below (the documented-member typing lives with the
    // FileStats fields).
    {"watch",
     [] {
         const Type cb =
             Type::function({Type::string(), Type::string()}, Type::voidType());
         return Type::function({Type::string(), cb}, fsWatcherObject());
     },
     "watch(path: string, callback: (eventType: string, filename: string) => void): FsWatcher",
     "Watches the file or directory at `path` and calls `callback(eventType, filename)` "
     "on every change, where `eventType` is \"change\" or \"rename\". The returned "
     "FsWatcher stops delivery when `close()` is called; the watcher keeps the "
     "program alive until then."},
};

// The documented fields of a `FileStats` value.
const DocRow kFsStatsRows[] = {
    {"size", "size: number", "The size of the file in bytes."},
    {"isFile", "isFile: boolean", "True when the entry is a regular file."},
    {"isDirectory", "isDirectory: boolean", "True when the entry is a directory."},
    {"modifiedAt", "modifiedAt: Date", "The time the file was last modified."},
};

}  // namespace

std::optional<Type> Analyzer::fsMember(const std::string& prop) {
    std::optional<Type> member = lookupMember(kFsRows, prop);
    if (!member) return member;
    // The stream's chunk size is a file-size quantity: `u64` in a Rust/C++
    // project, `number` in a TypeScript project.
    if (prop == "readFileStream" && member->parts.size() > 1) {
        member->parts[1] =
            stdIntegerType(StdIntegerKind::FileSize, options_.typeSystem);
    }
    return member;
}

// =============================================================================
// Receiver field reads
// =============================================================================
// The documented FileStats fields (docs 04). A known field gets its real type,
// so `stats.modifiedAt.toISOString()` chains into the date family; any other
// property returns nullopt and keeps the permissive ambient fallthrough. The
// matching codegen read is emitFsMemberRead (src/std/fs/codegen.cpp).
std::optional<Type> Analyzer::fsReceiverMember(const Type& recv,
                                               const std::string& prop) {
    if (recv.kind == TypeKind::Object && recv.name == kFsWatcher) {
        // The FsWatcher's one documented member (docs/ledger H §fs.watch).
        if (prop == "close") return Type::function({}, Type::voidType());
        return std::nullopt;
    }
    if (recv.kind != TypeKind::Object || recv.name != kFileStats) {
        return std::nullopt;
    }
    if (prop == "size") {
        return stdIntegerType(StdIntegerKind::FileSize, options_.typeSystem);
    }
    if (prop == "isFile" || prop == "isDirectory") return Type::boolean();
    if (prop == "modifiedAt") return Type::named(TypeKind::Class, "Date");
    return std::nullopt;
}

std::vector<std::string> fsMemberNames() { return memberNames(kFsRows); }

StdMemberDoc fsMemberDoc(const std::string& prop) {
    return memberDoc(kFsRows, prop);
}

StdMemberDoc fsStatsMemberDoc(const std::string& prop) {
    return memberDoc(kFsStatsRows, prop);
}

std::vector<std::string> fsStatsMemberNames() {
    return docRowNames(kFsStatsRows);
}

}  // namespace cp3
