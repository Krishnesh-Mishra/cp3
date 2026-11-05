// =============================================================================
// src/std/string/sema_docs.cpp - the documentation of the string method surface.
//
// OWNERSHIP: the std/string family (src/std/string/**). The member TYPING of
// the string surface lives in the compiler core (`stringMember`,
// src/sema/src/checks/member_check.cpp); the DOCUMENTATION lives here, with
// the family, so the editor can describe every string member the compiler
// knows.
//
// The descriptions are the documented ones
// (docs/language-spec/09-arrays-strings.md, "String" section) plus the UTF-8
// rules of the same chapter: cp3 strings are UTF-8, `str[i]` indexes raw
// bytes and `str.charAt(i)` returns the visual character.
//
// INTEGER-WIDTH NOTE: the signatures below keep the TypeScript `number`
// spelling (the doc-table precedent of the other std families). The exact
// widths live in cp3/host/std_integer.hpp and are applied by the runtime
// (cp3/std/string.hpp) and codegen (codegen.cpp): `length` is the length
// width, `charAt`/`at`/`slice`/`substring`/`indexOf`/`lastIndexOf` indices
// and `indexOf` results are signed indices, `repeat`'s count is a count,
// `padStart`/`padEnd`'s target is a length, and `str[i]` is a byte value
// (`charCodeAt`/`codePointAt` stay `number`: their NaN sentinel has no
// exact width). The member TYPING that must carry those widths is in the
// core (`stringMember`, outside this family) - see the audit report.
// =============================================================================
#include "detail/std_tables.hpp"

#include <string>
#include <vector>

namespace cp3 {

namespace {

const DocRow kStringDocRows[] = {
    {"length", "length: number",
     "The length of the string in bytes. cp3 strings are UTF-8, so a "
     "multi-byte character counts as its byte length."},
    {"charAt", "charAt(index: number): string",
     "Returns the character at `index` as a one-character string. Unlike "
     "`str[index]`, which returns a raw byte, this walks the UTF-8 encoding "
     "and returns the whole character."},
    {"at", "at(index: number): string",
     "Returns the character at `index`."},
    {"charCodeAt", "charCodeAt(index: number): number",
     "Returns the byte value of the character at `index`."},
    {"codePointAt", "codePointAt(index: number): number",
     "Returns the Unicode scalar value of the code point at code-point "
     "`index` (the unit `charAt` counts, not JS's UTF-16 code units). "
     "An out-of-range position returns `NaN`."},
    {"matchAll", "matchAll(regexp: RegExp): string[][]",
     "Returns every match of a global (`g`) regexp as an array of "
     "match arrays (`[full, group1, ...]`, the `exec` shape). The regexp "
     "must carry the `g` flag; the scan stops after the first empty match."},
    {"fromCodePoint", "fromCodePoint(...codes: number[]): string",
     "Constructs a string by UTF-8 encoding each Unicode scalar code. "
     "A code outside 0..0x10FFFF, or a surrogate, is a RangeError."},
    {"raw", "raw(chunk: string): string | raw(parts: string[], ...subs: string[]): string",
     "Returns `chunk` unchanged (no escape processing), or joins the "
     "`parts` chunks with the interpolated substitutions between them. "
     "This is the call form of `String.raw`; CP3 has no tagged templates."},
    {"startsWith", "startsWith(search: string): boolean",
     "True when the string starts with `search`. Backed by "
     "`std::string::starts_with`."},
    {"endsWith", "endsWith(search: string): boolean",
     "True when the string ends with `search`. Backed by "
     "`std::string::ends_with`."},
    {"includes", "includes(search: string): boolean",
     "True when the string contains `search`. Backed by `std::string::find`."},
    {"repeat", "repeat(count: number): string",
     "Returns the string repeated `count` times."},
    {"padStart", "padStart(length: number, pad?: string): string",
     "Pads the start of the string with `pad` (a space by default) until it "
     "is `length` characters long."},
    {"padEnd", "padEnd(length: number, pad?: string): string",
     "Pads the end of the string with `pad` (a space by default) until it is "
     "`length` characters long."},
    {"toUpperCase", "toUpperCase(): string",
     "Returns the string in upper case."},
    {"toLowerCase", "toLowerCase(): string",
     "Returns the string in lower case."},
    {"trim", "trim(): string",
     "Returns the string without the leading and trailing whitespace."},
    {"trimStart", "trimStart(): string",
     "Returns the string without the leading whitespace. Backed by "
     "`absl::StripLeadingAsciiWhitespace`."},
    {"trimEnd", "trimEnd(): string",
     "Returns the string without the trailing whitespace. Backed by "
     "`absl::StripTrailingAsciiWhitespace`."},
    {"indexOf", "indexOf(search: string, fromIndex?: number): number",
     "Returns the index of the first occurrence of `search`, or `-1` when "
     "the string does not contain it. Backed by `std::string::find`."},
    {"lastIndexOf", "lastIndexOf(search: string, fromIndex?: number): number",
     "Returns the index of the last occurrence of `search`, or `-1` when the "
     "string does not contain it."},
    {"slice", "slice(start?: number, end?: number): string",
     "Returns the part of the string from `start` up to (but not including) "
     "`end`. Backed by `std::string::substr`."},
    {"substring", "substring(start?: number, end?: number): string",
     "Returns the part of the string between `start` and `end`. Backed by "
     "`std::string::substr`."},
    {"replace", "replace(search: string, replacement: string): string",
     "Returns the string with the first occurrence of `search` replaced."},
    {"replaceAll", "replaceAll(search: string, replacement: string): string",
     "Returns the string with every occurrence of `search` replaced. Backed "
     "by `absl::StrReplaceAll`."},
    {"split", "split(separator: string | RegExp): string[]",
     "Splits the string at every occurrence of `separator` and returns the "
     "parts as an array."},
};

}  // namespace

StdMemberDoc stringMemberDoc(const std::string& prop) {
    return memberDoc(kStringDocRows, prop);
}

std::vector<std::string> stringMemberNames() {
    return docRowNames(kStringDocRows);
}

}  // namespace cp3
