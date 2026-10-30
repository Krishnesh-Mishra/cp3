---
title: Regular expressions
chapter: Chapter 3 · Standard library
---
# Regular expressions

Regular expressions are a core CP3 literal, not an imported `std/regex` module. A literal such as `/pattern/flags` creates a `RegExp` value. CP3 uses a PCRE2-backed runtime with a deliberately documented surface and several differences from JavaScript's UTF-16 semantics.

## Construct a regular expression

Write a pattern between slashes and place supported flags after the closing slash:

```ts
const word = /cp3/i;
const digits = /[0-9]+/g;
```

The lexer recognizes the literal and validates its pattern/flag form. Unknown or duplicate flags are errors. Escape `/` when it is part of the pattern, and use a normal string only when the API expects a string rather than a regular expression value.

The runtime compiles the pattern for execution. A pattern that passes lexical checks can still fail in the underlying regex engine; preserve the diagnostic or runtime message when reporting such a case.

## Flags

The runtime supports these flags:

| Flag | Behavior |
| --- | --- |
| `g` | Global matching; `test` and `exec` retain match position across calls. |
| `i` | Case-insensitive matching. |
| `m` | Multiline anchors. |
| `s` | Dot matches line terminators. |
| `u` | UTF mode in the PCRE2 backend. |
| `v` | Accepted as UTF mode in the current runtime. |
| `y` | Sticky matching at the current match position. |
| `d` | Accepted, but match indices are not exposed. |

The `flags` property reports canonical flag order. The current documented set is `dgimsuvy`; unsupported flags are rejected. Use only flags listed here rather than assuming that the host engine's entire option set is exposed.

## Instance API

| Member | Type | Result |
| --- | --- | --- |
| `test(subject)` | `(string) => boolean` | Whether the pattern matches. |
| `match(subject)` | `(string) => string[]` | Match strings in the CP3 result shape. |
| `exec(subject)` | `(string) => string[]` | One match and capture groups in the CP3 result shape. |
| `source` | `string` | Pattern text. |
| `flags` | `string` | Canonical flags. |

The surface is closed: an undocumented property is a compile-time member error. Regex values can be printed as `/source/flags`.

## Match results

CP3 uses `string[]` for match results. A no-match operation returns an empty array rather than JavaScript's `null`, because the current type system does not expose the corresponding `string[] | null` result here.

For a single non-global match, the result contains the full match followed by capture groups:

```ts
const parts = /(\d+)-(\w+)/.exec("42-cp3");
// parts[0] is the full match; following entries contain capture groups.
```

A group that did not participate is represented by an empty string, not `undefined`. With the `g` flag, `match(subject)` returns all whole matches and does not include capture groups, following the tested JavaScript-style global shape.

`exec` returns one match per call. With stateful flags, call it repeatedly to process later matches and stop when the result is empty. The global or sticky cursor resets after a failed match.

## Stateful `g` and `y` behavior

Regex literals with `g` or `y` retain match state. Copies made by assigning the regex to another binding share the same underlying state, like a reference value. A fresh evaluation of the literal creates a fresh regex object.

The current surface does not expose `lastIndex` as a readable or writable property, even though the engine maintains the state internally. Therefore, code cannot set the next offset directly through that property.

Sticky matching (`y`) only succeeds at the current position. A global expression (`g`) searches from its current position. Tests cover repeated calls, failure reset, and sticky behavior; do not assume calls are independent when reusing a stateful regex.

## Using a regex with strings

`string.split(separator)` accepts either a string separator or a `RegExp`:

```ts
const fields = "red,blue,green".split(/,/);
const words = "red   blue".split(/\s+/);
```

Regex splitting includes participating capture groups in the returned array. If the subject has no match, the result contains the subject. An empty match advances by one byte in the current implementation. The split operation does not read or update the regex's `g`/`y` cursor state.

## Unicode and indexing boundary

CP3 strings are UTF-8 byte strings. Regex offsets and the underlying engine operate on UTF-8 byte positions, not JavaScript UTF-16 code units. ASCII patterns and text generally follow the familiar behavior, but non-ASCII `.` matching and offsets can differ from JavaScript. Do not use a regex match offset as a CP3 code-point index without conversion.

The `u` and `v` flags select UTF handling in the current engine (`v` is treated as `u`); this does not make CP3's offsets UTF-16-based or provide grapheme segmentation. See [Strings](#03-standard-library/15-strings) for the language's byte-length and code-point rules.

## Engine and portability

The runtime uses a pinned PCRE2 static archive behind a CP3 wrapper. This avoids exposing PCRE2 headers in generated source, but it also means pattern corner cases are governed by CP3's adapter and the pinned engine version. Do not assume all JavaScript RegExp syntax, error wording, or Unicode edge cases are identical.

If the engine or supported flags change, update both the regex tests and this page. A pattern that works on one backend revision should be validated against the CP3 conformance corpus before being relied on for protocol or security decisions.

## Safety and performance

Regex engines can spend substantial time on ambiguous patterns and large inputs. Avoid nested ambiguous quantifiers on untrusted text. Apply input size limits, and test worst-case inputs for patterns that process network or user-provided data.

The API here is synchronous. A costly regex operation runs on the calling thread. If an input can be large, measure it with representative patterns and include adversarial cases. A benchmark on one easy string does not establish worst-case safety.

## Tests and related pages

The `tests/cases/regex` family covers literal flags, duplicate and unknown flags, invalid patterns, capture groups, no-match results, global cursor state, sticky state, and multiline flags. The `arrays-strings` cases cover passing a regex to `split`.

- [Strings](#03-standard-library/15-strings)
- [Standard library overview](#03-standard-library/00-overview)
- [Errors and exceptions](#01-language/06-modules-and-errors/02-errors)
