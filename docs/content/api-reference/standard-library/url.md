---
title: URL
---
# URL

`std/url` provides percent-encoding helpers plus the `URL` and `URLSearchParams` classes. Both named bindings and the default namespace are part of the closed surface; unknown members are compile-time errors:

```ts
import { URL, URLSearchParams } from "std/url";
import url from "std/url";
```

`std/url/types` publishes `URL` and `URLSearchParams` for annotation positions. Parsing follows RFC 3986 (see [URL extensions](./url-extensions.md) for the full base-resolution and normalization contract).

## Percent encoding

| Member | Signature | Notes |
| --- | --- | --- |
| `encodeURIComponent` | `encodeURIComponent(value: string): string` | Escapes everything except `A-Z a-z 0-9 - _ . ! ~ * ' ( )` |
| `decodeURIComponent` | `decodeURIComponent(value: string): string` | Decodes `%HH`; malformed sequences left as-is (no `URIError`) |
| `encodeURI` | `encodeURI(value: string): string` | Leaves URI delimiters (`; / ? : @ & = + $ , #`) intact |
| `decodeURI` | `decodeURI(value: string): string` | Decodes `%HH`; malformed sequences left as-is |

Byte-oriented over UTF-8; non-ASCII input is escaped byte-by-byte.

## `URL`

`new URL(input: string, base?: string): URL` — parses with an RFC 3986 parser and resolves a relative `input` against `base`. A relative input without a base, or an unparseable input, throws a catchable `Error` (`TypeError: Invalid URL`).

| Member | Type | Notes |
| --- | --- | --- |
| `href` | `string` | Serialized URL |
| `protocol` | `string` | Scheme with trailing colon, lowercased |
| `host` | `string` | Hostname with port when present; `""` when no authority |
| `hostname` | `string` | Lowercased hostname |
| `port` | `string` | Port digits or `""` when absent or the scheme default (80 → http/ws; 443 → https/wss) |
| `pathname` | `string` | Path with dot segments removed; `"/"` for a pathless authority URL |
| `search` | `string` | Query with leading `?`, or `""` |
| `hash` | `string` | Fragment with leading `#`, or `""` |
| `origin` | `string` | `scheme://host[:port]` for special schemes; `""` otherwise |
| `username` / `password` | `string` | Percent-encoded userinfo; split at the last `@`, then the first `:` |
| `searchParams` | `URLSearchParams` | Handle for this URL's query (see below) |
| `toString()` | `() => string` | Same serialization as `href` |

The ten string components are construction-time snapshots. Default ports are stripped and dot segments removed in serialization. There are no component setters and no live `search`/`href` rebinding — `searchParams` mutations do not rewrite `search` or `href`; build a new `URL` for a fresh snapshot.

## `URLSearchParams`

`new URLSearchParams(): URLSearchParams` (empty) — `new URLSearchParams(query: string): URLSearchParams` (parses `name=value&…`; a leading `?` is skipped). Pairs follow the `application/x-www-form-urlencoded` byte set: alphanumerics plus `* - . _` stay, space encodes as `+`, everything else as uppercase `%HH`; `+` decodes as space; malformed `%HH` parses as literal text.

| Member | Signature | Notes |
| --- | --- | --- |
| `append` | `append(name: string, value: string): undefined` | Appends at the end, duplicates preserved |
| `get` | `get(name: string): string \| undefined` | First value, or `undefined` |
| `getAll` | `getAll(name: string): string[]` | Every value, in order |
| `has` | `has(name: string): boolean` | Presence |
| `set` | `set(name: string, value: string): undefined` | Replaces the first occurrence, drops later duplicates; appends when absent |
| `delete` | `delete(name: string): boolean` | Removes every pair; whether any were removed |
| `toString` | `toString(): string` | `a=1&b=2`; no leading `?` |
| `entries` | `entries(): [string, string][]` | Pairs as tuples, in order |

Behavior details: [guide](../../standard-library/19-url.md)
