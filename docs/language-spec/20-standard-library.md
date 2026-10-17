# 20. Standard library

Core data-type operations (array/string methods, regex literals) are built into the language itself - no import needed. Everything that reaches beyond core types into OS/runtime functionality is an explicit import:

```
std/
├── console   (.log/.warn/.info/.input)
├── JSON      (.stringify/.parse, generic .parse<T> with runtime shape validation)
├── Math
├── fs        (Node-style, Sync-suffixed + async + stream variants)
├── Date      (immutable - every "change" method returns a new Date)
├── process   (.exec buffered, .spawn event-based, .argv, .env, .exit)
├── path
├── os
├── events    (EventEmitter-style: .on/.emit/.off, pure cp3, no native dependency)
├── crypto    (randomUUID, sha256/512, AES256 - libsodium-backed)
├── env       (.load(path), .get<T>(key, default) - built on top of process)
├── collections (Map/Set, 1:1 wrapping std::unordered_map/std::unordered_set)
├── buffer    (raw binary data handling)
└── network   (fetch - exact JS shape, not axios - plus WebSocket/WebSocketServer and an HTTP server, all backed by uWebSockets)
```

```cp3
import console from "std/console";
import Math from "std/math";

console.log(Math.sqrt(25));
```

**Written in cp3 itself, not hand-written C++.** The standard library is authored as ordinary `.cp3` source, same language as everything else - no separate C++ codebase to maintain in parallel. Where a module genuinely needs to reach real OS/C++ functionality underneath (actual file handles, math intrinsics, cryptographic primitives), it does so through the same **native import** bridge used for wrapping external C++ libraries ([§19](19-c-cpp-interoperability.md)) - e.g. `fs` internally uses `import native "cpp/filesystem"` to reach real file APIs; `crypto` internally uses `import native "libsodium"` for hashing and AES-256 encryption.

**`std/network` - reinstated for beta one, evolved further from an initial `std/server`.** A previous draft of this doc removed server support entirely, reasoning it would mean wrapping Drogon (a large, template-heavy C++ framework - a bad fit given beta one's native-interop scope explicitly excludes templates, [§19](19-c-cpp-interoperability.md)). That reasoning was sound *against Drogon specifically*, but dropping the capability along with it was a strategic overreach: it left the language's own headline pitch (the Express-vs-cp3 comparison in the intro) with nothing to actually demonstrate on day one. The fix isn't to wrap Drogon - it's to build directly on what beta one already requires, and consolidate further than the first reinstatement did: `fetch`, an HTTP server, and WebSocket support now live together in one `std/network` module, using the same libuv event loop that `async`/`await` already mandates for the client side, plus **uWebSockets** (widely regarded as among the fastest HTTP/WebSocket C++ libraries available, handling both in the same event loop) for the server/WebSocket side. No heavy general-purpose C++ web framework - full API and scope in [STDLIB-README.md](../standard-library/00-overview.md).

**`std/webview` remains dropped** - nothing in this reconsideration applies to it; it could still become a third-party package later, same as before.

## `setTimeout` / `setInterval` / `clearTimeout` - core language builtins, not std

Because these only mean anything in terms of the event loop the language itself already provides ([§14](14-async.md), Async), they are **global builtins, no import required** - same as JS - rather than a std module:

```cp3
let id = setTimeout(() => console.log("later"), 1000);
clearTimeout(id);

let intervalId = setInterval(() => console.log("tick"), 500);
clearInterval(intervalId);
```

**Full module-by-module detail (exact function signatures, what's deferred) lives in the companion [STDLIB-README.md](../standard-library/00-overview.md)**, tracked separately so the core language spec here isn't blocked on every std function being named.

**Bundled with the compiler - never a package to install.** Unlike a third-party dependency, `std/*` ships built into the `cp3` toolchain itself. `import console from "std/console"` works immediately in any project, with no `cp3 install`, no entry in `package.json`, and no `.cp3/packages/` folder involved - it's always available, the same way importing from `std::` needs no setup in C++.

