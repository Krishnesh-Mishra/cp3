// =============================================================================
// src/std/fs/sema.cpp - `std/fs` surface typing.
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
// fs is a CLOSED family (std_check.cpp isClosedStdNamespace): a member outside
// the table is CP3305 with these names as candidates instead of a CP3002 skip.
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

#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

// The receiver markers (shared with src/std/fs/codegen.cpp).
const char* const kFileStats = "$fs:FileStats";
const char* const kReadStream = "$fs:ReadStream";
const char* const kWriteStream = "$fs:WriteStream";

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
    {"readFileSync", [] { return readFileParams(Type::string()); }},
    {"writeFileSync", [] { return writeParams(Type::voidType()); }},
    {"appendFileSync", [] { return writeParams(Type::voidType()); }},
    {"existsSync", [] { return Type::function({Type::string()}, Type::boolean()); }},
    {"mkdirSync", [] { return mkdirParams(Type::voidType()); }},
    {"readdirSync", [] { return Type::function({Type::string()}, Type::array(Type::string())); }},
    {"statSync", [] { return Type::function({Type::string()}, fsStatsObject()); }},
    {"unlinkSync", [] { return Type::function({Type::string()}, Type::voidType()); }},
    {"readFile", [] { return readFileParams(promiseString()); }},
    {"writeFile", [] { return writeParams(promiseVoid()); }},
    {"appendFile", [] { return writeParams(promiseVoid()); }},
    {"mkdir", [] { return mkdirParams(promiseVoid()); }},
    {"readdir", [] { return Type::function({Type::string()}, promiseNames()); }},
    {"stat", [] { return Type::function({Type::string()}, promiseStats()); }},
    {"unlink", [] { return Type::function({Type::string()}, promiseVoid()); }},
    {"readFileStream",
     [] { return withOptionalTail(Type::function({Type::string(), Type::number()},
                                                 fsReadStreamObject())); }},
    {"writeFileStream",
     [] { return Type::function({Type::string()}, fsWriteStreamObject()); }},
};

}  // namespace

std::optional<Type> Analyzer::fsMember(const std::string& prop) {
    return lookupMember(kFsRows, prop);
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
    if (recv.kind != TypeKind::Object || recv.name != kFileStats) {
        return std::nullopt;
    }
    if (prop == "size") return Type::number();
    if (prop == "isFile" || prop == "isDirectory") return Type::boolean();
    if (prop == "modifiedAt") return Type::named(TypeKind::Class, "Date");
    return std::nullopt;
}

std::vector<std::string> fsMemberNames() { return memberNames(kFsRows); }

}  // namespace cp3
