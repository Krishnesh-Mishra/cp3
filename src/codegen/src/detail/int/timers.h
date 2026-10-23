// =============================================================================
// src/codegen/src/detail/timers.h - the core timer builtin names
// (spec section 14 / STDLIB docs section 03).
//
// Shared by the call dispatcher (emit/call.cpp) and the value-call lowering
// (emit/call_args.cpp): a bare global with one of these names lowers onto the
// runtime timer surface with no import required.
// =============================================================================
#pragma once

#include <string>

namespace cp3 {
namespace detail {

inline bool isTimerBuiltinName(const std::string& name) {
    return name == "setTimeout" || name == "setInterval" ||
           name == "clearTimeout" || name == "clearInterval" || name == "sleep";
}

}  // namespace detail
}  // namespace cp3
