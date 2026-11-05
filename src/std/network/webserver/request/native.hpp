// =============================================================================
// src/std/network/webserver/request/native.hpp - webserver `req` helpers native row.
//
// OWNERSHIP: the webserver/request family (src/std/network/webserver/request/**).
// One row of the D68 native-package table; the rest of the table never needs
// to change for this module. Header-only family: no vendored library and no
// runtime TU; the row supplies the generated-code include tree, selected when
// a program imports "std/network/webserver/request" (the HttpRequest helpers
// build on the std/network header, which is therefore listed first).
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

inline const NativePackage kNetworkwebserverRequestNativePackage{
    "std/network/webserver/request",
    {"src/std/network/webserver/request/include", "src/std/network/include",
     "src/std/json/include", "src/std/buffer/include",
     "src/std/collections/include"},
    {},
    {},
    {},
    {},
    // Parallel to runtimeSources (empty: header-only). Selected when the
    // generated code references the webserver request helpers; the std/network
    // row already covers the HttpRequest type itself.
    {""},
    {},
};

}  // namespace cp3::cli::detail
