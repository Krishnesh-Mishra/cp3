// =============================================================================
// src/std/io/sema.cpp - `std/io` surface typing and documentation.
//
// OWNERSHIP: the std/io family (src/std/io/**). One per-module check unit; the
// namespace dispatch row lives in std_check.cpp.
//
// Surface (v0.2, phases 1-2):
//   waitReadable(fd: number): Promise<void>
//   waitWritable(fd: number): Promise<void>
//   pollUntilDone(getSocket: () => number, checkProgress: () => number):
//       Promise<void>
//   POLL_READING / POLL_WRITING / POLL_DONE / POLL_FAILED: number
//
// Phase 3 (`import native async "<lib>"`) is deliberately not part of this
// table; it is deferred until the reactor tests pass.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/std_tables.hpp"

namespace cp3 {
namespace {

Type promiseVoid() { return promiseOf(Type::voidType()); }
Type numberThunk() { return Type::function({}, Type::number()); }

Type pollUntilDoneType() {
    return Type::function({numberThunk(), numberThunk()}, promiseVoid());
}

const MemberRow kIoRows[] = {
    {"waitReadable",
     [] { return Type::function({Type::number()}, promiseVoid()); },
     "waitReadable(fd: number): Promise<void>",
     "Waits until the socket descriptor `fd` is readable. Only the awaiting "
     "async function suspends; timers and other waiters keep running. The "
     "promise rejects on poll error, hangup, or an invalid/closed descriptor."},
    {"waitWritable",
     [] { return Type::function({Type::number()}, promiseVoid()); },
     "waitWritable(fd: number): Promise<void>",
     "Waits until the socket descriptor `fd` is writable. Only the awaiting "
     "async function suspends; timers and other waiters keep running. The "
     "promise rejects on poll error, hangup, or an invalid/closed descriptor."},
    {"pollUntilDone",
     [] { return pollUntilDoneType(); },
     "pollUntilDone(getSocket: () => number, checkProgress: () => number): Promise<void>",
     "Repeatedly calls `checkProgress` and waits for the socket returned by "
     "`getSocket`. `POLL_DONE` resolves, `POLL_FAILED` rejects, "
     "`POLL_READING`/`POLL_WRITING` wait for readability/writability. "
     "`getSocket` is called again on every iteration because the fd may change "
     "during connect."},
    {"POLL_READING", [] { return Type::number(); },
     "POLL_READING: number",
     "Progress state for `pollUntilDone`: wait until the socket is readable."},
    {"POLL_WRITING", [] { return Type::number(); },
     "POLL_WRITING: number",
     "Progress state for `pollUntilDone`: wait until the socket is writable."},
    {"POLL_DONE", [] { return Type::number(); },
     "POLL_DONE: number",
     "Progress state for `pollUntilDone`: the operation is complete."},
    {"POLL_FAILED", [] { return Type::number(); },
     "POLL_FAILED: number",
     "Progress state for `pollUntilDone`: the operation failed; the promise "
     "rejects."},
};

}  // namespace

std::optional<Type> Analyzer::ioMember(const std::string& prop) {
    return lookupMember(kIoRows, prop);
}

std::vector<std::string> ioMemberNames() { return memberNames(kIoRows); }

StdMemberDoc ioMemberDoc(const std::string& prop) {
    return memberDoc(kIoRows, prop);
}

}  // namespace cp3
