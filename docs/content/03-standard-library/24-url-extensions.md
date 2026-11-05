---
title: URL extensions
chapter: Chapter 3 · Standard library
---
# URL extensions

`std/url` is documented in [URL and query strings](19-url.md). This page covers the extension behaviors layered on top of that surface: RFC 3986 base resolution, normalization, IPv6/userinfo handling, form-urlencoded serialization details, and permissive percent-decoding. It lists the same import forms and full signatures so it can be read alongside the core page.

```ts
import { URL, URLSearchParams } from "std/url";
import url from "std/url";
```

Both the named bindings (for the classes) and the default namespace (for the percent-encoding helpers) are part of the closed surface. Unknown members are compile-time errors. No import is needed for `URL` beyond `std/url`.

## API summary (exact types)

| Member | Signature (exact types) | Semantics |
| --- | --- | --- |
| `new URL(input)` | `URL(input: string): URL` | Parses an absolute URL with the hand-written RFC 3986 parser. Relative input without a base, or unparseable input, throws a catchable `Error` (`TypeError: Invalid URL`). |
| `new URL(input, base)` | `URL(input: string, base: string): URL` | Resolves `input` against `base` per RFC 3986 §5.2. The base must itself parse as an absolute URL; otherwise it throws a catchable `Error`. |
| `URL.href` | `href: string` | Serialized URL (snapshot at construction). Pathless authority URLs serialize with `/`. |
| `URL.protocol` | `protocol: string` | Scheme with trailing colon, lowercased (e.g. `"https:"`). |
| `URL.host` | `host: string` | Hostname with port when present (IPv6 bracketed). `""` when no authority. |
| `URL.hostname` | `hostname: string` | Lowercased hostname without port or brackets. `""` when no authority. |
| `URL.port` | `port: string` | Port digits without colon, or `""` when absent or the scheme default (`80` for `http`/`ws`, `443` for `https`/`wss`). |
| `URL.pathname` | `pathname: string` | Path with dot segments removed. `"/"` for a pathless authority URL. |
| `URL.search` | `search: string` | Query with leading `?`, or `""` when absent. Snapshot; not rewritten by `searchParams` mutations. |
| `URL.hash` | `hash: string` | Fragment with leading `#`, or `""` when absent. |
| `URL.origin` | `origin: string` | `scheme://host[:port]` for special schemes (`http`, `https`, `ws`, `wss`, `ftp`); `""` otherwise (opaque schemes, `file:`). |
| `URL.username` | `username: string` | Percent-encoded userinfo username, or `""`. Userinfo splits at the last `@`, then at the first `:`. |
| `URL.password` | `password: string` | Percent-encoded userinfo password, or `""`. |
| `URL.searchParams` | `searchParams: URLSearchParams` | Live handle for this URL's query. Mutations do not rewrite `search` / `href` automatically. |
| `new URLSearchParams()` | `URLSearchParams(): URLSearchParams` | Empty pair list. |
| `new URLSearchParams(query)` | `URLSearchParams(query: string): URLSearchParams` | Parses `name=value&...`; a leading `?` is skipped. A part without `=` yields `name` with empty value; `+` decodes as space. |
| `sp.append` | `append(name: string, value: string): void` | Appends a pair to the end, preserving duplicates and order. |
| `sp.get` | `get(name: string): string \| undefined` | First value for `name`, or `undefined` when absent. |
| `sp.getAll` | `getAll(name: string): string[]` | Every value for `name`, in order. |
| `sp.has` | `has(name: string): boolean` | True when `name` appears at least once. |
| `sp.set` | `set(name: string, value: string): void` | Replaces the first occurrence and drops later duplicates; appends when absent. |
| `sp.delete` | `delete(name: string): boolean` | Removes every pair with `name`; returns whether any were removed. |
| `sp.toString` | `toString(): string` | Serializes as `name=value&...` (`application/x-www-form-urlencoded`): `A-Z a-z 0-9 * - . _` stay, space encodes as `+`, everything else as uppercase `%HH`. No leading `?`. |
| `sp.entries` | `entries(): [string, string][]` | Pairs as `[name, value]` tuples, in order. |
| `url.encodeURIComponent` | `encodeURIComponent(value: string): string` | Escapes everything except `A-Z a-z 0-9 - _ . ! ~ * ' ( )`. |
| `url.decodeURIComponent` | `decodeURIComponent(value: string): string` | Decodes `%HH`; malformed sequences are left as-is. |
| `url.encodeURI` | `encodeURI(value: string): string` | Leaves URI delimiters (`; / ? : @ & = + $ , #`) intact; escapes the rest. |
| `url.decodeURI` | `decodeURI(value: string): string` | Decodes `%HH`; malformed sequences are left as-is. |

All string components are snapshots taken at construction. `href` serialization follows RFC 3986 §5.3 in the WHATWG reading (default ports stripped, IPv6 bracketed, userinfo serialized as `user:pass@` only when a password is present).

## Semantics

### Base resolution (RFC 3986 §5.2)

`new URL(input, base)` implements the RFC 3986 §5.2 transform:

- Absolute `input` (with its own scheme) wins; dot segments are removed from its path.
- Authority-relative input (`//host/path`) inherits only the base scheme.
- Absolute-path input (`/a/b`) inherits scheme and authority, replacing the path.
- Relative-path input (`c/d`, `../e`) merges with the base path (the base's last segment is replaced), then dot segments are removed.
- Empty input path inherits the base path; the query is inherited only when the input has no query. The fragment always comes from the input.

The parser trims ASCII whitespace/C0 from the ends of each input. Scheme must match `alpha (alphanumerics / + - .)*` before the first `:` that precedes any `/`; a non-conforming scheme or an empty input throws. An authority with an empty host or a non-digit port is invalid and throws.

### Normalization

- Scheme and hostname are lowercased (ASCII).
- Dot segments are removed per §5.2.4 (`/a/b/../c` → `/a/c`; `/./` collapses; `/.` and `/..` at the end resolve to `/`).
- Default ports are stripped: `http`/`ws` `:80` and `https`/`wss` `:443` serialize without the port.
- A pathless authority URL reports and serializes `pathname` as `"/"`.

### IPv6 and userinfo

- Bracketed IPv6 (`[::1]:8080`) parses with `hostname` unbracketed (`::1`), `host` bracketed (`[::1]:8080`), and `port` separate. An unclosed bracket is treated as a (bogus) host rather than a throw; a bare-number authority (e.g. `http://123/`) is a hostname, not a port.
- Userinfo (text before the last `@`) splits at the first `:` into `username` / `password`. Both are percent-encoded; decoding is the caller's job.

### Form-urlencoded rules (`URLSearchParams`)

Parse and serialize use the WHATWG `application/x-www-form-urlencoded` byte set: alphanumerics plus `*` `-` `.` `_` stay, a space encodes as `+`, and every other byte is uppercase `%HH`. `+` decodes to space before `%HH` decoding, so a literal `%2B` still decodes to `+`. Malformed `%HH` sequences parse as literal text. `toString()` joins pairs with `&` and `=` and adds no leading `?`.

### Permissive percent-decoding

`decodeURIComponent` / `decodeURI` decode `%HH` (hex case-insensitive) and leave malformed sequences as-is. This deliberately mismatches ECMAScript, where `URIError` is thrown. `URL` construction follows the same permissive stance: a malformed percent escape in the input does not by itself make the URL invalid.

### Failure behavior

Invalid-URL construction throws a catchable CP3 `Error` (`Thrown::make`), never a raw `std::exception` the `catch` model cannot take. The message is `"TypeError: Invalid URL"`.

## Limits

- No setters on `URL` components and no `URL.toString` in this surface: re-serialize by constructing a new `URL`. Component assignment outside the documented snapshot reads is a compile-time error.
- No live `search` / `href` rebinding: `URLSearchParams` mutations do not rewrite `search` or `href` (unlike WHATWG live binding). Read `u.searchParams.get(...)` for current values, or construct a new `URL` when a fresh snapshot must reflect the query.
- No IDNA or punycode: non-ASCII hostnames are lowercased as bytes and kept as-is; there is no internationalized-domain conversion.
- No `atob` / `btoa` here: those globals are base64 <-> byte text (`string` to `string`, exactly one argument) and belong with the buffer codec, not with URL parsing.
- Percent-encoding is byte-oriented over UTF-8: `encodeURI` / `encodeURIComponent` iterate the string's bytes, so non-ASCII input is escaped byte-by-byte as `%HH` sequences of its UTF-8 encoding.

## Related pages

- [Core URL surface](19-url.md)
- [Buffers](11-buffer.md) — hex/base64 byte rendering
- [Text encoding](20-text.md) — `TextEncoder` / `TextDecoder` (UTF-8-only)
- [Network](13-network.md) — fetch and HTTP server APIs that consume URLs
