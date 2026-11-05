// =============================================================================
// src/std/time/sema.cpp - `std/time` surface typing and documentation.
//
// OWNERSHIP: the std/time family (src/std/time/**). One per-module check unit;
// the core dispatch rows (std_check.cpp / member_check.cpp) forward here, so
// the rest of sema never needs to change for this module.
//
// The tables mirror the v0.2 ledger (ROADMAP §C) `stopwatch` entry:
//   static stopwatch(): Stopwatch;
//   instance start(): void; stop(): void; elapsed(): number; reset(): void.
//
// Both the namespace object (`time.stopwatch`) and a Stopwatch VALUE
// (`sw.elapsed()`) are CLOSED: a member outside the tables is CP3305 with the
// implemented names as candidates, never an untyped fallthrough. The tables
// drive member typing, the candidate lists and the editor documentation at
// once, so the three can never drift apart.
//
// `elapsed()` is milliseconds as a fractional `number` (the runtime returns
// `double`; there is no std integer width for a fractional duration).
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

Type stopwatchValue() { return Type::named(TypeKind::Class, "Stopwatch"); }

Type zeroToStopwatch() { return Type::function({}, stopwatchValue()); }
Type zeroToVoid() { return Type::function({}, Type::voidType()); }
Type zeroToNumber() { return Type::function({}, Type::number()); }

// The namespace object's static (the ledger spelling `stopwatch()`).
const MemberRow kTimeStaticRows[] = {
    {"stopwatch", zeroToStopwatch, "stopwatch(): Stopwatch",
     "Creates a new stopped `Stopwatch`. The clock is monotonic "
     "(`std::chrono::steady_clock`); see `elapsed()` for the unit."},
};

// The members of a Stopwatch value.
const MemberRow kTimeInstanceRows[] = {
    {"start", zeroToVoid, "start(): void",
     "(Re)starts the watch from now, discarding any previously accumulated "
     "time. Calling `start()` on a running watch restarts it."},
    {"stop", zeroToVoid, "stop(): void",
     "Freezes the interval: afterwards `elapsed()` returns the same value "
     "every time. Stopping a stopped watch is a no-op."},
    {"elapsed", zeroToNumber, "elapsed(): number",
     "Milliseconds since `start()`: the live interval while running, the "
     "frozen value after `stop()`, and `0` before the first `start()`. "
     "Monotonic, so it is never negative."},
    {"reset", zeroToVoid, "reset(): void",
     "Stops the watch and zeroes the elapsed time."},
};

}  // namespace

std::optional<Type> Analyzer::timeMember(const std::string& prop) {
    return lookupMember(kTimeStaticRows, prop);
}

std::optional<Type> Analyzer::timeInstanceMember(const Type& recv,
                                                 const std::string& prop) {
    (void)recv;
    return lookupMember(kTimeInstanceRows, prop);
}

// A Stopwatch value's nominal type (TypeKind::Class, name "Stopwatch").
bool isStopwatchClass(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "Stopwatch";
}

std::vector<std::string> timeMemberNames() {
    return memberNames(kTimeStaticRows);
}

std::vector<std::string> timeInstanceMemberNames() {
    return memberNames(kTimeInstanceRows);
}

StdMemberDoc timeMemberDoc(const std::string& prop) {
    return memberDoc(kTimeStaticRows, prop);
}

StdMemberDoc timeInstanceMemberDoc(const std::string& prop) {
    return memberDoc(kTimeInstanceRows, prop);
}

}  // namespace cp3
