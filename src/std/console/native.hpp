// =============================================================================
// src/std/console/native.hpp - `std/console` native backend row (D72).
//
// OWNERSHIP: the std/console family (src/std/console/**). One row of the D68
// native-package table; the rest of the table never needs to change for this
// module.
//
// Pure runtime module: no vendored library. The row supplies the family ABI
// tree and its runtime TUs (console.warn/.error plus the D71 stdin pump); both
// are selected only when a program imports "std/console" (D68). libuv is never
// linked; D76's ShowWindow resolves through the toolchain's default system
// libraries (verified in the wave report's link evidence).
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kConsoleNativePackage{
    "std/console",
    {"src/std/console/include"},
    {},
    {"src/std/console/runtime.cpp", "src/std/console/runtime_pump.cpp",
     "src/std/console/runtime_pump_read.cpp"},
    {},
    {},
    // `console.log`/`.info` of plain values lower straight onto `println` and
    // need none of these TUs; the D49 object/array renderer, warn/error/
    // clear/assert/hide/show do need runtime.cpp, and the D71 stdin pump pair
    // only exists for input/readKey/on/off. The pump pair moves together
    // (readerMain and the post helpers live either side of pump.hpp).
    {"::cp3::runtime::console::|::cp3::runtime::formatObject|"
     "::cp3::runtime::formatValue|::cp3::runtime::formatNested|"
     "::cp3::runtime::writeLine",
     "::cp3::runtime::console::input(|::cp3::runtime::console::readKey(|"
     "::cp3::runtime::console::on(|::cp3::runtime::console::off(",
     "::cp3::runtime::console::input(|::cp3::runtime::console::readKey(|"
     "::cp3::runtime::console::on(|::cp3::runtime::console::off("},
};

}  // namespace cp3::cli::detail
