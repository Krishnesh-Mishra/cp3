// =============================================================================
// src/std/date/sema.cpp - `std/date` surface typing.
//
// OWNERSHIP: the std/date family (src/std/date/**). One per-module check unit;
// the core dispatch rows (std_check.cpp / member_check.cpp) forward here, so
// the rest of sema never needs to change for this module.
//
// The typed tables mirror docs/standard-library/04 section Date:
//   static now(): Date; fromISOString(s: string): Date;
//   toISOString(): string; toString(): string;
//   getFullYear/getMonth/getDate/getHours/getMinutes/getSeconds(): number;
//   addDays/addHours/addMinutes/addSeconds(n: number): Date;
//   isBefore(other: Date): boolean; isAfter(other: Date): boolean;
//   diffMs(other: Date): number.
//
// Both the namespace object (`Date.now`) and a Date VALUE (`d.getFullYear()`)
// are CLOSED: a member outside the tables is CP3305 with the implemented names
// as candidates instead of a CP3002 skip. The tables drive member typing and
// the candidate lists at once, so the two can never drift apart.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

Type dateValue() { return Type::named(TypeKind::Class, "Date"); }

Type zeroToDate() { return Type::function({}, dateValue()); }
Type stringToDate() { return Type::function({Type::string()}, dateValue()); }
Type zeroToString() { return Type::function({}, Type::string()); }
Type zeroToNumber() { return Type::function({}, Type::number()); }
Type numberToDate() { return Type::function({Type::number()}, dateValue()); }
Type dateToBoolean() { return Type::function({dateValue()}, Type::boolean()); }
Type dateToNumber() { return Type::function({dateValue()}, Type::number()); }

// The namespace object's statics (docs 04).
const MemberRow kDateStaticRows[] = {
    {"now", zeroToDate},
    {"fromISOString", stringToDate},
};

// The members of a Date value. Every "change" returns a NEW Date: the type
// says so, and the runtime keeps the receiver untouched.
const MemberRow kDateInstanceRows[] = {
    {"toISOString", zeroToString}, {"toString", zeroToString},
    {"getFullYear", zeroToNumber}, {"getMonth", zeroToNumber},
    {"getDate", zeroToNumber},     {"getHours", zeroToNumber},
    {"getMinutes", zeroToNumber},  {"getSeconds", zeroToNumber},
    {"addDays", numberToDate},     {"addHours", numberToDate},
    {"addMinutes", numberToDate},  {"addSeconds", numberToDate},
    {"isBefore", dateToBoolean},   {"isAfter", dateToBoolean},
    {"diffMs", dateToNumber},
};

}  // namespace

std::optional<Type> Analyzer::dateMember(const std::string& prop) {
    return lookupMember(kDateStaticRows, prop);
}

// The receiver is part of the signature for symmetry with the other families;
// the Date surface does not depend on it today.
std::optional<Type> Analyzer::dateInstanceMember(const Type& recv,
                                                 const std::string& prop) {
    (void)recv;
    return lookupMember(kDateInstanceRows, prop);
}

// A Date value's nominal type (TypeKind::Class, name "Date").
bool isDateClass(const Type& t) {
    return t.kind == TypeKind::Class && t.name == "Date";
}

std::vector<std::string> dateMemberNames() {
    return memberNames(kDateStaticRows);
}

std::vector<std::string> dateInstanceMemberNames() {
    return memberNames(kDateInstanceRows);
}

}  // namespace cp3
