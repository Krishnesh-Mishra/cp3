// =============================================================================
// src/dap/src/detail/mi.hpp - the slice of gdb's MI grammar the adapter needs.
//
// OWNERSHIP: cp3-debug workstream (src/dap/**). PRIVATE header.
//
// gdb mixes plain lines, stream records (`~"..."`, `@"..."`, `&"..."`), async
// records (`*stopped,...`) and result records (`^done,...`) on one stream. Only
// the value part after the record class is structured; this is the parser for
// it. Deliberately tolerant: an unparsable value is reported once and the
// record is ignored, never a crash and never a stall.
// =============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace cp3::dap {

// One MI value: a scalar (unquoted token or unescaped quoted string), a tuple
// (`{name=value,...}`) or a list (`[value,...]`, whose elements may carry a
// `name=` tag, as in `stack=[frame={...}]`).
struct MiValue {
    enum class Kind { Scalar, Tuple, List };
    Kind kind = Kind::Scalar;
    std::string text;  // Scalar: the value, unescaped when it arrived quoted
    std::string name;  // list element tag, e.g. "frame"
    std::vector<std::pair<std::string, MiValue>> fields;  // Tuple, in order
    std::vector<MiValue> items;                           // List, in order
};

// Parses one value at `pos` and advances `pos` past it. False on malformed
// input (the caller then treats the whole record as opaque).
bool parseMiValue(const std::string& in, std::size_t& pos, MiValue& out);

// Parses the `name=value,...` body that follows a result class ("^done,...").
bool parseMiBody(const std::string& in, std::size_t pos, MiValue& out);

// First field called `name`, or nullptr.
const MiValue* miField(const MiValue& value, const std::string& name);

// Text of the scalar field `name` ("" when missing or not a scalar).
std::string miText(const MiValue& value, const std::string& name);

// Unescapes a C-style escaped MI string (no surrounding quotes).
std::string miUnescape(const std::string& in);

// Quotes a string for use inside an MI command (paths in -break-insert).
std::string miQuote(const std::string& in);

}  // namespace cp3::dap
