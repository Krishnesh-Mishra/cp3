---
title: URL extensions
---
# URL extensions

`std/url` is documented in [URL](./url.md); this page covers what is layered on top of that surface: RFC 3986 base resolution, normalization, IPv6/userinfo handling, form-urlencoded serialization and permissive percent-decoding. Same import forms and full signatures, read alongside the core page:

```ts
import { URL, URLSearchParams } from "std/url";
import url from "std/url";
```

`std/url/types` publishes the class names for annotation positions.

## API summary (exact types)

| Member | Signature | Notes |
| --- | --- | --- |
| `new URL(input)` | `URL(input: string): URL` | Absolute URL with an RFC 3986 parser; relative input without a base, or unparseable input, throws a catchable `Error` (`TypeError: Invalid URL`) |
| `new URL(input, base)` | `URL(input: string, base: string): URL` | RFC 3986 §5.2 transform; the base must itself parse as absolute or it throws |
| `URL.href` | `href: string` | Serialized snapshot; pathless authority URLs serialize with `/` |
| `URL.protocol` | `protocol: string` | Scheme + trailing colon, lowercased |
| `URL.host` | `host: string` | Hostname with port (IPv6 bracketed); `""` when no authority |
| `URL.hostname` | `hostname: string` | Lowercased, no port or brackets |
| `URL.port` | `port: string` | Digits, or `""` when absent or the scheme default (`80` for http/ws, `443` for https/wss) |
| `URL.pathname` | `pathname: string` | Dot segments removed; `"/"` for a pathless authority URL |
| `URL.search` | `search: string` | Query with leading `?`, or `""`; not rewritten by `searchParams` mutations |
| `URL.hash` | `hash: string` | Fragment with leading `#`, or `""` |
| `URL.origin` | `origin: string` | `scheme://host[:port]` for special schemes (`http`, `https`, `ws`, `wss`, `ftp`); `""` otherwise, incl. `file:` |
| `URL.username` / `URL.password` | `string` | Percent-encoded; userinfo splits at the last `@`, then the first `:` |
| `URL.searchParams` | `searchParams: URLSearchParams` | Mutable handle; mutations do not rewrite `search`/`href` |
| `new URLSearchParams()` / `new URLSearchParams(query)` | `URLSearchParams(): URLSearchParams` / `URLSearchParams(query: string): URLSearchParams` | Empty list; or parses `name=value&…` (a leading `?` is skipped; a part without `=` yields an empty value; `+` decodes as space) |
| `sp.append` | `append(name: string, value: string): undefined` | Appends a pair at the end |
| `sp.get` / `sp.getAll` / `sp.has` | `get(name): string \| undefined`, `getAll(name): string[]`, `has(name): boolean` | First value; all values in order; presence |
| `sp.set` | `set(name: string, value: string): undefined` | Replaces the first, drops later duplicates; appends when absent |
| `sp.delete` | `delete(name: string): boolean` | Removes every pair with `name`; whether any were removed |
| `sp.toString` | `toString(): string` | `name=value&…` form-urlencoded; no leading `?`; `* - . _` stay, space encodes `+`, else uppercase `%HH` |
| `sp.entries` | `entries(): [string, string][]` | Pairs in order |
| `url.encodeURIComponent` / `url.decodeURIComponent` | `(value: string) => string` | Component percent-encoding; decode is permissive |
| `url.encodeURI` / `url.decodeURI` | `(value: string) => string` | Whole-URI encoding (delimiters intact); decode is permissive |

## Semantics

- **Base resolution (RFC 3986 §5.2):** absolute `input` (with scheme) wins; authority-relative (`//host/path`) inherits only the scheme; absolute-path (`/a/b`) inherits scheme + authority, replacing the path; relative-path merges into the base path (last segment replaced), then dot segments are removed; empty input path inherits the base path, and the query is inherited only when the input has none; the fragment always comes from the input. Inputs are trimmed of leading/trailing ASCII whitespace/C0; a scheme must match `alpha (alphanumerics | + - .)*` before the first `:`; a non-conforming scheme, empty host or non-digit port throws.
- **Normalization:** scheme and hostname lowercased (ASCII); dot segments removed per §5.2.4; default ports stripped (`http`/`ws` `80`, `https`/`wss` `443`); pathless authority URLs report and serialize `pathname` as `"/"`.
- **IPv6 and userinfo:** `[::1]:8080` parses with `hostname` unbracketed, `host` bracketed, `port` separate; an unclosed bracket is treated as a bogus host rather than a throw; a bare-number authority (`http://123/`) is a hostname, not a port. Userinfo splits at the last `@`, then the first `:`; both parts are percent-encoded — decoding is the caller's job.
- **Form-urlencoded (`URLSearchParams`):** WHATWG `application/x-www-form-urlencoded` byte set — alphanumerics plus `* - . _` stay, space encodes as `+`, all else uppercase `%HH`; `+` decodes to space before `%HH` decoding (so a literal `%2B` still decodes to `+`); malformed `%HH` parses as literal text; `toString()` joins with `&`/`=` and adds no leading `?`.
- **Permissive percent-decoding:** malformed `%HH` in `decodeURIComponent`/`decodeURI` is left as-is (deliberately unlike ECMAScript `URIError`); `URL` construction is equally permissive — a malformed percent escape does not by itself invalidate the URL. Error contract: invalid-URL construction throws a catchable CP3 `Error` whose message is `"TypeError: Invalid URL"`.

## Limits

- No `URL` component setters; no live `search`/`href` rebinding (unlike WHATWG). Read `u.searchParams.get(...)` for current values, or construct a new `URL`.
- No IDNA/punycode — non-ASCII hostnames are lowercased as bytes and kept as-is. No `atob`/`btoa` here. Percent-encoding is byte-oriented over UTF-8.

Behavior details: [guide](../../standard-library/24-url-extensions.md)
