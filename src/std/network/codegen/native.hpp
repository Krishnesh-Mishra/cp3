// =============================================================================
// src/std/network/native.hpp - `std/network` native backend row.
//
// OWNERSHIP: the std/network family (src/std/network/**). One row of the D68
// native-package table; the rest of the table never needs to change for this module.
//
// Backend: libuv v1.53.0 (client side / event loop) plus uWebSockets v20.80.0
// with its in-tree uSockets (server + WebSocket side), pinned static builds -
// DECISIONS D70 + vendor/third_party/MANIFEST.md. TLS is linked from the vendored
// OpenSSL archives (vendor/third_party/openssl) and is lit when the generated program
// names the TLS surface (CP3_TLS_OPENSSL); per-message deflate is off by
// design, so `UWS_NO_ZLIB` is on every TU that sees the uWS headers.
// =============================================================================
#pragma once

#include "detail/native/native_packages.hpp"

namespace cp3::cli::detail {

//   compile: -DUWS_NO_ZLIB -DCP3_TLS_OPENSSL -I vendor/third_party/uWebSockets/include
//            -I vendor/third_party/libuv/include (uSockets headers include <uv.h>)
//            -I vendor/third_party/openssl/include (the OpenSSL TLS backend for
//            createTlsServer/connectTls, selected via CP3_TLS_OPENSSL).
//   link (Windows): -L vendor/third_party/uWebSockets/lib -luSockets
//                   -L vendor/third_party/libuv/lib -luv -L vendor/third_party/openssl/lib -lssl -lcrypto
//                   plus libuv's Windows system libraries; linux uses arch-specific paths.
inline const NativePackage kNetworkNativePackage{
    "std/network",
    // The family ABI tree (D72) first; src/std/json/include serves
    // runtime_server.cpp's cp3/std/json.hpp include, and the buffer /
    // collections roots serve cp3/std/network.hpp's includes (res.headers and
    // req.params/query/headers are Map<string, string>, arrayBuffer is Buffer).
    {"src/std/network/include", "src/std/json/include",
     "src/std/buffer/include", "src/std/collections/include",
     "src/std/url/include",
     "src/std/network/dns/include",
     "src/std/network/tcp/include",
     "src/std/network/udp/include",
     "src/std/network/tls/include",
     "src/std/network/webserver/body-parse/include",
     "src/std/network/webserver/cookie/include",
     "src/std/network/webserver/middleware/include",
     "src/std/network/webserver/request/include",
     "src/std/network/webserver/response/include",
     "src/std/network/webserver/router/include",
     "src/std/network/webserver/routing",
     "src/std/network/webserver/settings/include",
     "src/std/network/webserver/regex-routing",
     "vendor/third_party/uWebSockets/include", "vendor/third_party/libuv/include",
     "vendor/third_party/unordered_dense/include", "vendor/third_party/fast_float/include",
     "vendor/third_party/libsodium/include", "vendor/third_party/xxhash/include",
     "vendor/third_party/openssl/include"},
    {"-DUWS_NO_ZLIB", "-DCP3_TLS_OPENSSL", "-DCP3_UWS_SSL"},
    {"src/std/network/runtime/runtime.cpp",
     "src/std/network/runtime/runtime_fetch.cpp",
     "src/std/network/runtime/runtime_fetch_io.cpp",
     "src/std/network/runtime/runtime_dns.cpp",
     "src/std/network/runtime/runtime_web.cpp",
     "src/std/network/runtime/runtime_webserver_regex.cpp",
     "src/std/network/server/runtime_server.cpp",
     "src/std/network/server/runtime_server_dispatch.cpp",
     "src/std/network/server/runtime_server_scaled.cpp",
     "src/std/network/ws/runtime_ws.cpp",
     "src/std/network/ws/runtime_ws_client.cpp",
     "src/std/network/ws/runtime_ws_frames.cpp",
     "src/std/network/webserver/body-parse/runtime_body_parse.cpp",
     "src/std/network/webserver/errors/runtime_errors.cpp",
     "src/std/network/webserver/middleware/runtime/middleware_engine.cpp",
     "src/std/network/webserver/regex-routing/runtime_regex_routing.cpp",
     // webserver/router/runtime_router.cpp + webserver/routing/runtime_routing.cpp
     // are intentionally NOT linked: they are a stale duplicate of the
     // header-inline webserver::Router in cp3/std/network.hpp (their
     // RouterOptions clashed with the public header and matchMount had no
     // matching declaration - the files cannot compile). The generated code
     // uses the inline Router, so these TUs contributed only compile/link
     // failures for any program whose text mentioned Router(/dispatchRouter.
     "src/std/network/webserver/response/runtime/runtime_webserver_response.cpp",
     "src/std/network/dns/runtime/runtime_dns_lookup.cpp",
     "src/std/network/tcp/runtime_tcp_socket.cpp",
     "src/std/network/tcp/runtime_tcp_client.cpp",
     "src/std/network/tcp/runtime_tcp_server.cpp",
     "src/std/network/tcp/runtime_tcp_reuseport.cpp",
     "src/std/network/udp/runtime_udp.cpp",
     "src/std/network/tls/runtime_tls.cpp",
     // cp3/std/network.hpp includes cp3/std/collections.hpp (headers/params/
     // query are Map<string,string>), so instantiating the Map references
     // bucketHashBytes; the seeded bucket-hash TU rides along (the collections
     // row's backend).
     "src/runtime/src/core/bucket_hash.cpp"},
    {},
    {"-lsodium", "-lxxhash"},
    // Parallel to runtimeSources. A program that imports std/network only for
    // fetch should not link the HTTP server or WebSocket runtime, and a
    // WebSocket program should not link the server. The dispatch and scaled
    // server TUs ride with runtime_server.cpp (listen/close reference the
    // scaled entry points, and the request glue references the dispatcher).
    // The regex-routing pair (runtime_webserver_regex.cpp +
    // runtime_regex_routing.cpp) is selected together by `addRegexRoute(` /
    // `PathSpec` in the generated text: a plain HTTP server without regex
    // routes links neither. bucket_hash rides along unconditionally because
    // the fetch, server and regex TUs all instantiate the std Map, which
    // references `bucketHashBytes` (this row only applies to network programs
    // anyway).
    {"", "fetch(", "fetch(", "dnsResolve(|dns::lookup(",
     "makeStaticMiddleware(|makeCorsMiddleware(|makeBodyLimitMiddleware(|Headers(|Request(|Response(|FormData(|AbortSignal::|createServerWithConfig(",
     "addRegexRoute(", "HttpServer(|createServerWithConfig(",
     "HttpServer(|createServerWithConfig(", "HttpServer(|createServerWithConfig(",
     "WebSocket", "WebSocket", "WebSocket",
     "makeJsonMiddleware(|makeUrlEncodedMiddleware(|body_parse::", "webserver::errors::", "MiddlewareEngine(|webserver::",
     "addRegexRoute(|matchPath(|PathSpec",
     "emitwebserverResponseCall|res.set(|setwebserverResponse|createServerWithConfig(|res.render(", "dns::lookup(|dnsResolve(",
     "TcpSocket|TcpServer|createTcpServer(|connectTcp(", "connectTcp(", "TcpServer|createTcpServer(", "TcpServer|createTcpServer(",
     "createUdpSocket(|UdpSocket", "createTlsServer(|connectTls(|TlsSocket|TlsServer|fetch(|WebSocket|makeTlsOptions(", ""},
    {{"windows-x64", {"vendor/third_party/uWebSockets/lib/windows-x64",
                       "vendor/third_party/libuv/lib/windows-x64",
                       "vendor/third_party/libsodium/lib/windows-x64",
                       "vendor/third_party/xxhash/lib/windows-x64",
                       "vendor/third_party/openssl/lib/windows-x64"},
      {"-luSockets", "-luv", "-lsodium", "-lxxhash", "-lssl", "-lcrypto",
       "-lws2_32", "-luserenv",
       "-liphlpapi", "-lole32", "-ldbghelp", "-lsynchronization", "-lcrypt32"}},
     {"windows-arm64", {"vendor/third_party/uWebSockets/lib/windows-arm64",
                       "vendor/third_party/libuv/lib/windows-arm64",
                       "vendor/third_party/libsodium/lib/windows-arm64",
                       "vendor/third_party/xxhash/lib/windows-arm64",
                       "vendor/third_party/openssl/lib/windows-arm64"},
      {"-luSockets", "-luv", "-lsodium", "-lxxhash", "-lssl", "-lcrypto",
       "-lws2_32", "-luserenv",
       "-liphlpapi", "-lole32", "-ldbghelp", "-lsynchronization", "-lcrypt32"}},
     {"linux-x64",
      {"vendor/third_party/uWebSockets/lib/linux-x64",
       "vendor/third_party/libuv/lib/linux-x64",
       "vendor/third_party/libsodium/lib/linux-x64",
       "vendor/third_party/xxhash/lib/linux-x64",
       "vendor/third_party/openssl/lib/linux-x64"},
      {"-luSockets", "-luv", "-lsodium", "-lxxhash", "-lssl", "-lcrypto",
       "-pthread", "-ldl", "-lrt"}},
     {"linux-arm64",
      {"vendor/third_party/uWebSockets/lib/linux-arm64",
       "vendor/third_party/libuv/lib/linux-arm64",
       "vendor/third_party/libsodium/lib/linux-arm64",
       "vendor/third_party/xxhash/lib/linux-arm64",
       "vendor/third_party/openssl/lib/linux-arm64"},
      {"-luSockets", "-luv", "-lsodium", "-lxxhash", "-lssl", "-lcrypto",
       "-pthread", "-ldl", "-lrt"}}},
};

}  // namespace cp3::cli::detail
