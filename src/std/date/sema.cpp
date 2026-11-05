// =============================================================================
// src/std/date/sema.cpp - `std/date` surface typing and documentation.
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
// as candidates, never an untyped fallthrough. The tables drive member typing,
// the candidate lists and the editor documentation at once, so the three can
// never drift apart.
//
// A Date is immutable: every "change" operation returns a NEW Date and leaves
// the receiver untouched (deliberately unlike JS's mutable Date).
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "cp3/host/std_integer.hpp"
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
Type stringToNumber() { return Type::function({Type::string()}, Type::number()); }
Type numberToNumber() { return Type::function({Type::number()}, Type::number()); }

// The namespace object's statics (docs 04).
const MemberRow kDateStaticRows[] = {
    {"now", zeroToDate, "now(): Date",
     "Returns the current date and time as a new `Date`."},
    {"fromISOString", stringToDate, "fromISOString(s: string): Date",
     "Parses an ISO-8601 date string into a `Date`."},
    {"parse", stringToNumber, "parse(s: string): number",
     "Parses an ISO-8601 date string into milliseconds since the epoch."},
};

// The members of a Date value. Every "change" returns a NEW Date: the type
// says so, and the runtime keeps the receiver untouched.
const MemberRow kDateInstanceRows[] = {
    {"toISOString", zeroToString, "toISOString(): string",
     "Returns the date and time as an ISO-8601 string."},
    {"toString", zeroToString, "toString(): string",
     "Returns the date and time as a human-readable string."},
    {"getFullYear", zeroToNumber, "getFullYear(): number",
     "Returns the year of the date."},
    {"getMonth", zeroToNumber, "getMonth(): number",
     "Returns the month of the date, 0-indexed (0 = January), matching JS."},
    {"getDate", zeroToNumber, "getDate(): number",
     "Returns the day of the month of the date."},
    {"getHours", zeroToNumber, "getHours(): number",
     "Returns the hour of the day of the date."},
    {"getMinutes", zeroToNumber, "getMinutes(): number",
     "Returns the minute of the hour of the date."},
    {"getSeconds", zeroToNumber, "getSeconds(): number",
     "Returns the second of the minute of the date."},
    {"getTime", zeroToNumber, "getTime(): number",
     "Returns milliseconds since the epoch."},
    {"getDay", zeroToNumber, "getDay(): number",
     "Returns the day of the week (0 = Sunday)."},
    {"getUTCFullYear", zeroToNumber, "getUTCFullYear(): number",
     "Returns the UTC year (same as `getFullYear`: all Dates are UTC)."},
    {"getUTCMonth", zeroToNumber, "getUTCMonth(): number",
     "Returns the UTC month, 0-indexed."},
    {"getUTCDate", zeroToNumber, "getUTCDate(): number",
     "Returns the UTC day of the month."},
    {"getUTCDay", zeroToNumber, "getUTCDay(): number",
     "Returns the UTC day of the week, 0 = Sunday."},
    {"getUTCHours", zeroToNumber, "getUTCHours(): number",
     "Returns the UTC hour of the day."},
    {"getUTCMinutes", zeroToNumber, "getUTCMinutes(): number",
     "Returns the UTC minute of the hour."},
    {"getUTCSeconds", zeroToNumber, "getUTCSeconds(): number",
     "Returns the UTC second of the minute."},
    {"setTime", numberToNumber, "setTime(ms: number): number",
     "Sets the date to `ms` milliseconds since the epoch, in place."},
    {"setFullYear", numberToNumber, "setFullYear(year: number): number",
     "Sets the UTC year in place."},
    {"setMonth", numberToNumber, "setMonth(month: number): number",
     "Sets the UTC month (0-indexed) in place."},
    {"setDate", numberToNumber, "setDate(day: number): number",
     "Sets the UTC day of the month in place."},
    {"setHours", numberToNumber, "setHours(hour: number): number",
     "Sets the UTC hour in place."},
    {"setMinutes", numberToNumber, "setMinutes(minute: number): number",
     "Sets the UTC minute in place."},
    {"setSeconds", numberToNumber, "setSeconds(second: number): number",
     "Sets the UTC second in place."},
    {"addDays", numberToDate, "addDays(n: number): Date",
     "Returns a NEW `Date` moved `n` days forward; the receiver is "
     "untouched."},
    {"addHours", numberToDate, "addHours(n: number): Date",
     "Returns a NEW `Date` moved `n` hours forward; the receiver is "
     "untouched."},
    {"addMinutes", numberToDate, "addMinutes(n: number): Date",
     "Returns a NEW `Date` moved `n` minutes forward; the receiver is "
     "untouched."},
    {"addSeconds", numberToDate, "addSeconds(n: number): Date",
     "Returns a NEW `Date` moved `n` seconds forward; the receiver is "
     "untouched."},
    {"isBefore", dateToBoolean, "isBefore(other: Date): boolean",
     "True when this date is earlier than `other`."},
    {"isAfter", dateToBoolean, "isAfter(other: Date): boolean",
     "True when this date is later than `other`."},
    {"diffMs", dateToNumber, "diffMs(other: Date): number",
     "Returns the difference between this date and `other` in milliseconds "
     "(this minus other)."},
};

}  // namespace

std::optional<Type> Analyzer::dateMember(const std::string& prop) {
    // `Date.parse` returns a timestamp: the exact `i64` milliseconds width in
    // a Rust/C++ project, `number` in a TypeScript one.
    if (prop == "parse") {
        return Type::function(
            {Type::string()},
            stdIntegerType(StdIntegerKind::Timestamp, options_.typeSystem));
    }
    return lookupMember(kDateStaticRows, prop);
}

// The receiver is part of the signature for symmetry with the other families;
// the Date surface does not depend on it today.
std::optional<Type> Analyzer::dateInstanceMember(const Type& recv,
                                                 const std::string& prop) {
    (void)recv;
    // The timestamp surface carries the exact `i64` milliseconds width in a
    // Rust/C++ project, `number` in a TypeScript one: the timestamp getters
    // and the millisecond setter. The component getters/setters and the
    // `add*` steps stay `number` (they are calendar fields).
    const StdIntegerKind kTimestamp = StdIntegerKind::Timestamp;
    if (prop == "getTime" || prop == "diffMs") {
        return Type::function(
            prop == "diffMs" ? std::vector<Type>{dateValue()} : std::vector<Type>{},
            stdIntegerType(kTimestamp, options_.typeSystem));
    }
    if (prop == "setTime") {
        const Type timestamp =
            stdIntegerType(kTimestamp, options_.typeSystem);
        return Type::function({timestamp}, timestamp);
    }
    if (prop == "setFullYear" || prop == "setMonth" || prop == "setDate" ||
        prop == "setHours" || prop == "setMinutes" || prop == "setSeconds") {
        // The parameter is a calendar field (`number`); the return is the
        // new timestamp.
        return Type::function(
            {Type::number()},
            stdIntegerType(kTimestamp, options_.typeSystem));
    }
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

StdMemberDoc dateMemberDoc(const std::string& prop) {
    return memberDoc(kDateStaticRows, prop);
}

StdMemberDoc dateInstanceMemberDoc(const std::string& prop) {
    return memberDoc(kDateInstanceRows, prop);
}

}  // namespace cp3
