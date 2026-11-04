// =============================================================================
// src/std/url/sema.cpp - `std/url` member typing and documentation.
//
// OWNERSHIP: the std/url family (src/std/url/**). The member table drives
// member typing, the CP3305 candidate list and the documentation the editor
// shows, so the three can never drift apart. URL is a CLOSED family: a member
// outside the table is CP3305 with these names as candidates, never an
// untyped fallthrough.
//
// The signatures and the descriptions are the documented ones
// (docs/standard-library/04-imported-std-modules.md, URL section): RFC 3986
// URI encoding and decoding.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

Type stringUnary() { return Type::function({Type::string()}, Type::string()); }

const MemberRow kUrlRows[] = {
    {"encodeURIComponent", &stringUnary,
     "encodeURIComponent(value: string): string",
     "Escapes all characters except A-Z a-z 0-9 - _ . ! ~ * ' ( ) (the RFC "
     "3986 unreserved marks plus the ECMAScript extra set)."},
    {"decodeURIComponent", &stringUnary,
     "decodeURIComponent(value: string): string",
     "Decodes %HH sequences; a malformed sequence is left as-is."},
    {"encodeURI", &stringUnary,
     "encodeURI(value: string): string",
     "Escapes all characters except the URI delimiters (; / ? : @ & = + $ , "
     "#), leaving the URI structure intact."},
    {"decodeURI", &stringUnary,
     "decodeURI(value: string): string",
     "Decodes %HH sequences; a malformed sequence is left as-is."},
};

}  // namespace

std::optional<Type> Analyzer::urlMember(const std::string& prop) {
    return lookupMember(kUrlRows, prop);
}

std::vector<std::string> urlMemberNames() {
    return memberNames(kUrlRows);
}

StdMemberDoc urlMemberDoc(const std::string& prop) {
    return memberDoc(kUrlRows, prop);
}

}  // namespace cp3
