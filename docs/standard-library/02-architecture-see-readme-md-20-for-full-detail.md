# Architecture (see `README.md` §20 for full detail)

The standard library is authored in cp3 itself, not hand-written C++. Where a module genuinely needs to reach real OS/C++ functionality underneath, it does so through the same import native bridge used for wrapping any external C++ library.

Bundled with the compiler — std/* modules are never a package to cp3 install. No package.json entry, no registry fetch, no .packages/ folder involved. Always available, same as std:: needs no setup in C++.

**`std/network` — reinstated for beta one, evolved from an initial `std/server`.** An earlier draft removed server support entirely on the reasoning that it would mean wrapping Drogon (large, template-heavy, a bad fit for beta-one's native-interop scope, which excludes templates — see [README.md §19](../language-spec/19-c-cpp-interoperability.md)). That reasoning held against Drogon specifically, but the capability itself is back, built differently — and consolidated further than the first reinstatement: `fetch`, an HTTP server, and WebSocket support now live together in one `std/network` module, backed by libuv (already required the moment `async`/`await` is used at all) for the client side and **uWebSockets** for the server/WebSocket side, with routing/request/response logic written in ordinary cp3. Full module spec below.

`std/webview` remains dropped, unaffected by the above — could still exist later as a third-party `.cp3pak` package.

