// =============================================================================
// src/std/json/sema.cpp - `std/json` member typing and documentation.
//
// OWNERSHIP: the std/json family (src/std/json/**). The member table moved
// here from src/sema/src/checks/std_check.cpp (D72 stage 2); that core unit
// keeps the namespace dispatch (stdNamespaceMember / stdNamespaceMemberNames)
// and forwards here, so the rest of sema never needs to change for this
// family.
//
// The same table drives member typing, the CP3305 candidate list and the
// documentation the editor shows, so the three can never drift apart. JSON is
// a CLOSED family (std_check.cpp isClosedStdNamespace): a member outside the
// table is CP3305 with these names as candidates, never an untyped fallthrough.
//
// The signatures and the descriptions are the documented ones
// (docs/content/03-standard-library/09-json.md).
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

// `(text: string) => unknown` / `(value: unknown, replacer?: ..., space?:
// number | string) => string` and the file-oriented async/sync forms below.
// The real call typing stays in tryInferJsonCall (call_check.cpp); these
// rows only mark the surface closed and give member reads a type. The second
// parameter's union keeps the CP3 indent form
// (`stringify(v, 2)`: a number) alongside the ECMAScript replacer form.
Type jsonParseSignature() {
    return Type::function({Type::string()}, Type::unknown());
}
Type jsonReplacerSignature() {
    return Type::function({Type::string(), Type::unknown()}, Type::unknown());
}
Type jsonStringifySignature() {
    const Type replacerOrIndent = Type::unionOf({
        Type::number(), jsonReplacerSignature(), Type::array(Type::string())});
    const Type space = Type::unionOf({Type::number(), Type::string()});
    return withOptionalTail2(
        Type::function({Type::unknown(), replacerOrIndent, space},
                       Type::string()));
}

// `(path: string) => unknown` — the `T` of `parseFile<T>` rides the explicit
// type argument exactly like `parse<T>`; the result type is bound in
// `tryInferJsonCall` (std/json namespace call inference).
Type jsonParseFileSignature() {
    return Type::function({Type::string()}, Type::unknown());
}
// `(path: string) => Promise<unknown>`
Type jsonParseFilePromiseSignature() {
    return promiseOf(Type::unknown());
}
// `(path: string, value: unknown) => void`
Type jsonStringifyToFileSignature() {
    return Type::function({Type::string(), Type::unknown()}, Type::voidType());
}
// `(path: string, value: unknown) => Promise<void>`
Type jsonStringifyToFilePromiseSignature() {
    return promiseOf(Type::voidType());
}
// `(text: string) => boolean`
Type jsonVerifySignature() {
    return Type::function({Type::string()}, Type::boolean());
}

const MemberRow kJsonRows[] = {
    {"parse", jsonParseSignature, "parse<T>(text: string): T",
     "Parses `text` as JSON.\n\n"
     "Without a type argument the result is `unknown` and has to be narrowed "
     "before use. With `parse<T>` the parsed value is validated against `T`'s "
     "shape at runtime, and a mismatch throws an ordinary catchable error."},
    {"stringify", jsonStringifySignature,
     "stringify<T>(value: T, replacer?: ((key: string, value: unknown) => unknown) | string[] | number, space?: number | string): string",
     "Serializes `value` as JSON text. `replacer` is an ECMAScript replacer "
     "callback `(key, value) => replacement` (return `undefined` to omit a "
     "property) or a `string[]` allowlist of property names; `space` (number "
     "or string) controls pretty-print indentation. For back-compat, "
     "`stringify(value, indentNumber)` is still accepted as the second "
     "argument."},
    {"parseFileSync", jsonParseFileSignature,
     "parseFileSync<T>(path: string): T",
     "Reads the file at `path` and parses it as JSON.\n\n"
     "Behaves like `parse<T>` on the file's text: the value is validated "
     "against `T`'s shape, and a mismatch throws an ordinary catchable "
     "error. A missing or unreadable file throws the same class of error "
     "(`json::ParseError`)."},
    {"parseFile", jsonParseFilePromiseSignature,
     "parseFile<T>(path: string): Promise<T>",
     "Reads the file at `path` and parses it as JSON on the async fs path.\n\n"
     "The promise resolves to the value validated against `T` (exactly like "
     "`parseFileSync<T>`) and rejects with the same error class on a read or "
     "shape failure."},
    {"stringifyToFileSync", jsonStringifyToFileSignature,
     "stringifyToFileSync<T>(path: string, value: T): void",
     "Serializes `value` as JSON text and overwrites the file at `path` "
     "(like `stringify(value)` followed by `fs.writeFileSync`). Throws on a "
     "write error."},
    {"stringifyToFile", jsonStringifyToFilePromiseSignature,
     "stringifyToFile<T>(path: string, value: T): Promise<void>",
     "Serializes `value` as JSON text and overwrites the file at `path` "
     "(like `stringify(value)` followed by `fs.writeFile`). The promise "
     "rejects on a write error."},
    {"verify", jsonVerifySignature,
     "verify<T>(text: string): boolean",
     "Returns whether `text` parses as JSON and conforms to `T`, with the "
     "same rules as `parse<T>` (extra keys are ignored; a missing or "
     "mistyped required field makes it return `false`). Never throws."},
};

}  // namespace

std::optional<Type> Analyzer::jsonMember(const std::string& prop) {
    return lookupMember(kJsonRows, prop);
}

std::vector<std::string> jsonMemberNames() { return memberNames(kJsonRows); }

StdMemberDoc jsonMemberDoc(const std::string& prop) {
    return memberDoc(kJsonRows, prop);
}

}  // namespace cp3
