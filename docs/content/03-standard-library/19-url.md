---
title: URL and query strings
chapter: Chapter 3 · Standard library
---
# URL and query strings

`std/url` provides percent-encoding helpers plus the `URL` and `URLSearchParams` classes, imported as named bindings:

```ts
import { URL, URLSearchParams } from "std/url";
import url from "std/url";
```

## Percent encoding

- `url.encodeURIComponent(value)` / `url.decodeURIComponent(value)` escape and decode all bytes except `A-Z a-z 0-9 - _ . ! ~ * ' ( )`.
- `url.encodeURI(value)` / `url.decodeURI(value)` leave the URI delimiters (`; / ? : @ & = + $ , #`) intact.
- Malformed `%HH` sequences are left as-is on decode (mismatching ECMAScript, which throws `URIError`); `URL` construction follows the same permissive stance.

## The `URL` class

`new URL(input, base?)` parses `input` with a hand-written RFC 3986 parser (no pattern matching). With two arguments, a relative `input` resolves against `base` (authority-relative, absolute-path and relative-path references follow RFC 3986 §5.2). A missing base for a relative input, or an unparseable input, throws a catchable `Error`.

```ts
const u = new URL("https://example.com:8080/a/b?q=1#top");
u.protocol; // "https:"
u.host;     // "example.com:8080"
u.hostname; // "example.com"
u.port;     // "8080" ("" when absent or the scheme default)
u.pathname; // "/a/b"
u.search;   // "?q=1"
u.hash;     // "#top"
u.origin;   // "https://example.com:8080"
u.username; // ""
u.password; // ""
u.href;     // the serialized URL
```

The ten string components are snapshots taken at construction. Default ports are stripped (`https://example.com:443/` serializes without `:443`); dot segments are removed from the path. `u.searchParams` is a `URLSearchParams` handle for the parsed query.

> `searchParams` mutations do not rewrite `search` or `href` automatically (unlike WHATWG live binding). Read `u.searchParams.get(...)` for current values, or construct a new `URL` when a fresh snapshot must reflect the query.

## The `URLSearchParams` class

`new URLSearchParams()` starts empty; `new URLSearchParams("a=1&b=2")` parses a query string (a leading `?` is skipped).

```ts
const sp = new URLSearchParams("a=1&b=2&a=3");
sp.get("a");      // "1" (undefined when absent)
sp.getAll("a");   // ["1", "3"]
sp.has("b");      // true
sp.append("c", "4");
sp.set("a", "9");     // replaces the first value, drops later duplicates
sp.delete("b");       // true; removes every "b" pair
sp.toString();    // "a=9&c=4"
sp.entries();     // [["a", "9"], ["c", "4"]]
```

Pairs use the `application/x-www-form-urlencoded` rules: `+` is a space on parse and encodes back as `+`; malformed `%HH` sequences parse as literal text. `get` returns the first value or `undefined`; `getAll` returns every value in order; `set` replaces the first occurrence and removes later duplicates (appending when absent).

## API summary

| Member | Type | Description |
| --- | --- | --- |
| `encodeURIComponent` / `decodeURIComponent` | `(s: string) => string` | Component percent-encoding |
| `encodeURI` / `decodeURI` | `(s: string) => string` | Whole-URI percent-encoding |
| `URL(input, base?)` | constructor | Parses/resolves with RFC 3986 rules |
| `URL.href`, `.protocol`, `.host`, `.hostname`, `.port`, `.pathname`, `.search`, `.hash`, `.origin`, `.username`, `.password` | `string` | Construction-time snapshot components |
| `URL.searchParams` | `URLSearchParams` | Query handle for this URL |
| `URLSearchParams(query?)` | constructor | Parses a query string |
| `append` / `set` | `(name, value) => undefined` | Append, or replace-first/drop-rest |
| `get` | `(name) => string \| undefined` | First value |
| `getAll` | `(name) => string[]` | All values |
| `has` | `(name) => boolean` | Presence |
| `delete` | `(name) => boolean` | Remove all pairs |
| `toString` | `() => string` | `a=1&b=2` serialization |
| `entries()` | `() => [string, string][]` | Pairs as tuples |

## Test and implementation boundary

The conformance cases live under `tests/cases/stdlib/02/url` (`url_*` and `urlsearchparams_*`). The implementation and semantic member tables are under `src/std/url`; unknown members are compile-time errors rather than dynamic property access. Together with the percent-encoding helpers, this module covers the RFC 3986 parser and the WHATWG `URL`/`URLSearchParams` surface for v0.2; setters on `URL` components, `URL.toString`, and live `search`/`href` rebinding are out of scope.
