// =============================================================================
// src/std/readline/sema.cpp - `std/readline` surface typing and documentation.
//
// OWNERSHIP: the std/readline family (src/std/readline/**). One per-module
// check unit; the namespace dispatch row lives in std_check.cpp (lead wiring,
// see the ledger report). New family, so nothing here imports shared state
// beyond detail/analyzer.h + detail/std_tables.hpp.
//
// Surface (ledger L):
//   readLine(): Promise<string>
//   readLines(): Promise<string[]>
//   readAll(): Promise<string>
//   createReader(): LineReader
// LineReader (marker `$readline:LineReader`, CLOSED shape):
//   readLine(): Promise<string>
//   readLines(): Promise<string[]>
//   on(event: "line" | "close", listener: (value: string) => void): void
//   close(): void
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

const char* const kLineReader = "$readline:LineReader";

Type promiseString() { return promiseOf(Type::string()); }
Type promiseStringArray() { return promiseOf(Type::array(Type::string())); }

/// `(value: string) => void` with a permissive parameter: the listener may
/// also take no parameter (codegen types the arrow contextually).
Type lineListenerType() {
    return Type::function({Type::string()}, Type::voidType());
}

const MemberRow kReadlineRows[] = {
    {"readLine",
     [] { return Type::function({}, promiseString()); },
     "readLine(): Promise<string>",
     "Reads the next line from stdin and resolves with it, terminator "
     "removed; resolves with `\"\"` on EOF instead of rejecting. Successive "
     "calls never lose bytes."},
    {"readLines",
     [] { return Type::function({}, promiseStringArray()); },
     "readLines(): Promise<string[]>",
     "Reads stdin to EOF and resolves with every line, terminators removed "
     "(`\\r\\n` counts as one terminator)."},
    {"readAll",
     [] { return Type::function({}, promiseString()); },
     "readAll(): Promise<string>",
     "Reads stdin to EOF and resolves with every byte (never rejects; EOF "
     "is the normal end)."},
    {"createReader",
     [] { return Type::function({}, Type::unknown()); },
     "createReader(): LineReader",
     "Creates a line reader over stdin: `.readLine()` / `.readLines()` read, "
     "`.on(\"line\" | \"close\", ...)` observes lines as they arrive, and "
     "`.close()` detaches that reader."},
};

// The documented members of a LineReader value.
const DocRow kLineReaderRows[] = {
    {"readLine", "readLine(): Promise<string>",
     "Resolves with the next line of stdin, terminator removed; `\"\"` on "
     "EOF (or immediately once this reader is closed)."},
    {"readLines", "readLines(): Promise<string[]>",
     "Resolves with every line of stdin read to EOF (empty once this "
     "reader is closed)."},
    {"on",
     "on(event: \"line\" | \"close\", listener: (value: string) => void): void",
     "Observes every completed line (`\"line\"`) or stdin EOF (`\"close\"`, "
     "once, with `\"\"`). A tap: it never consumes a line a pending "
     "`readLine` also sees."},
    {"close", "close(): void",
     "Detaches this reader: its listeners stop firing and its later reads "
     "resolve immediately. Stdin stays open for other consumers."},
};

}  // namespace

Type Analyzer::readlineObjectType(const std::string& name) {
    if (name != kLineReader) return Type::unknown();
    std::vector<Field> fields;
    fields.push_back(Field{"readLine", Type::function({}, promiseString()), false});
    fields.push_back(
        Field{"readLines", Type::function({}, promiseStringArray()), false});
    fields.push_back(Field{"on",
                           Type::function({Type::string(), lineListenerType()},
                                          Type::voidType()),
                           false});
    fields.push_back(Field{"close", Type::function({}, Type::voidType()), false});
    Type object = makeObjectType(std::move(fields), false, false, {});
    // Same marker convention as the process family: codegen recognises the
    // shape by NAME, so register the fields under the marker name too.
    const auto label = shapes_.find(object.name);
    if (label != shapes_.end()) {
        Shape shape = label->second;
        object.name = name;
        shapes_[name] = std::move(shape);
    }
    return object;
}

std::optional<Type> Analyzer::readlineMember(const std::string& prop) {
    if (prop == "createReader") {
        return Type::function({}, readlineObjectType(kLineReader));
    }
    return lookupMember(kReadlineRows, prop);
}

std::vector<std::string> readlineMemberNames() {
    std::vector<std::string> names = memberNames(kReadlineRows);
    names.push_back("createReader");
    return names;
}

std::vector<std::string> readlineMemberArgumentValues(const std::string& prop,
                                                        std::size_t argumentIndex) {
    if (argumentIndex == 0 && prop == "on") return {"line", "close"};
    return {};
}

StdMemberDoc readlineMemberDoc(const std::string& prop) {
    if (prop == "createReader") {
        StdMemberDoc doc;
        doc.signature = "createReader(): LineReader";
        doc.body =
            "Creates a line reader over stdin: `.readLine()` / `.readLines()` "
            "read, `.on(\"line\" | \"close\", ...)` observes lines as they "
            "arrive, and `.close()` detaches that reader.";
        return doc;
    }
    return memberDoc(kReadlineRows, prop);
}

StdMemberDoc readlineLineReaderDoc(const std::string& prop) {
    return memberDoc(kLineReaderRows, prop);
}

std::vector<std::string> readlineLineReaderMemberNames() {
    return docRowNames(kLineReaderRows);
}

}  // namespace cp3
