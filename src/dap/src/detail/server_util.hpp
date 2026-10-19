// =============================================================================
// src/dap/src/detail/server_util.hpp - JSON field helpers for the DAP server.
//
// OWNERSHIP: cp3-debug workstream (src/dap/**). PRIVATE header.
//
// Shared by the transport loop (server.cpp) and the request handlers
// (server_requests.cpp); both need to read scalar fields out of request
// arguments and echo the client's request ids.
// =============================================================================
#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

#include "detail/dap_json.hpp"

namespace cp3::dap {

// The string member `key`, or "" when absent or not a string.
inline std::string fieldString(const Json& object, std::string_view key) {
    const Json* value = object.find(key);
    const std::string* text = value != nullptr ? value->asString() : nullptr;
    return text != nullptr ? *text : std::string();
}

// The numeric member `key` as an int, or `fallback`.
inline int fieldInt(const Json& object, std::string_view key, int fallback) {
    const Json* value = object.find(key);
    if (value == nullptr || !value->isNumber()) return fallback;
    return static_cast<int>(std::strtol(value->number.c_str(), nullptr, 10));
}

// The request `seq` exactly as the client wrote it: DAP ids round-trip
// verbatim, so the lexeme is echoed, not re-formatted.
inline std::string requestSeqText(const Json& message) {
    const Json* seq = message.find("seq");
    if (seq != nullptr && seq->isNumber()) return seq->number;
    return "0";
}

}  // namespace cp3::dap
