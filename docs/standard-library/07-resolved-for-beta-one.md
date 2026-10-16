# Resolved for beta one

- Exact function signatures within each module — specified inline above per module.
- `console.log`/`.info` → stdout, `.warn`/`.error` → stderr — see the `console` section above.
- Raw socket/TCP networking — deferred past beta one, not included.
- Generics runtime reification mechanism — usage-based per-type shape descriptors, only for types that actually appear as `T` in `JSON.parse<T>`; full detail in [README.md §23](../language-spec/23-open-design-questions.md).
- **`fetch`, `server`, and WebSocket support consolidated into a single `std/network` module**, replacing the earlier separate axios-style `std/fetch` and `std/server` (no WebSocket) modules — `fetch` switched to the exact native JS `fetch(url, options)` shape (no more `.get()`/`.post()`/`.custom()`), backed internally by uWebSockets for the server/WebSocket side. Full detail in the `network` section above.
- **`console.clear()`, `console.assert()`, and `console.readKey(): Promise<string>` added** — found missing while writing real example programs (an interactive CLI menu needs `readKey`; a screen-clearing TUI needs `clear`), matching the project's own principle that writing real code is the best way to surface spec gaps.
- **`Math.gcd`/`.lcm`/`.clamp`/`.lerp`/`.cbrt` added** — plain `<numeric>`/`<cmath>`, not a linear-algebra dependency (Eigen solves a different problem: matrices, not these scalar helpers).
- **Global `sleep(ms)`, `parseInt(str)`, and `parseFloat(str)` added** as core-language builtins ([README.md](../language-spec/00-overview.md)'s "built into the language" list) — `sleep` is a thin `Promise` wrapper over `setTimeout`, no new mechanism; `parseInt`/`parseFloat` match JS's own placement as globals rather than string methods.

