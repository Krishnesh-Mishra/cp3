# Explicitly deferred / out of scope

- Testing - no built-in test runner in std. Left to third-party/external packages, not part of core std.
- std/webview - considered, then deliberately dropped from the core plan entirely (see Architecture note above). Not "deferred" in the sense of "coming later officially" - just not part of the roadmap; anyone can build it as a third-party package whenever the need arises. (`std/server` is no longer a separate module at all - its capability lives in `std/network` now, alongside `fetch` and WebSocket support; see the `network` module section above.)
- Raw TCP/UDP socket networking beyond HTTP/WebSocket (`std/network`) - **decided: deferred past beta one.** Not included in beta one at all; don't build against it.

