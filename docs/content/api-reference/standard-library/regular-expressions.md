---
title: Regular expressions
---
# Regular expressions

Regular expressions are a core CP3 literal, not an imported module — there is no `std/regex`. A literal `/pattern/flags` creates a `RegExp` value, compiled by a vendored PCRE2 build. CP3 strings are UTF-8 byte strings; offsets are byte positions, not JavaScript UTF-16 code units.

## Flags

The documented set is `dgimsuvy`, reported in canonical flag order by `flags`:

| Flag | Behavior |
| --- | --- |
| `g` | Global matching; `test`/`exec` retain position across calls |
| `i` | Case-insensitive |
| `m` | Multiline anchors |
| `s` | `.` matches line terminators |
| `u` | UTF mode (PCRE2) |
| `v` | Accepted as UTF mode (treated as `u`) |
| `y` | Sticky matching at the current position |
| `d` | Accepted; match indices are not exposed |

Unknown or duplicate flags are compile-time errors. The pattern must escape `/`.

## `RegExp` members (closed surface — anything else is a member error)

| Member | Signature | Notes |
| --- | --- | --- |
| `test` | `test(subject: string): boolean` | Whether the pattern matches |
| `match` | `match(subject: string): string[]` | Every match of the pattern in `subject` |
| `exec` | `exec(subject: string): string[]` | One match + capture groups; `g`/`y` cursors resume from the last call; a failed match resets the cursor |
| `source` | `source: string` | Pattern text |
| `flags` | `flags: string` | Canonical flag order |

No match returns an empty array (not `null`). A group that did not participate is `""`, not `undefined`. With `g`, `match` returns whole matches only (no groups). `g`/`y` state is shared by bindings copied from the literal; `lastIndex` is not readable or writable. Named groups are not part of this surface.

## String methods that take a `RegExp`

| Member | Signature | Notes |
| --- | --- | --- |
| `split` | `split(separator: string \| RegExp): string[]` | Regex splitting includes participating capture groups; no match → the whole subject; an empty match advances one byte; does not touch a `g`/`y` cursor |
| `replace` | `replace(search: string \| RegExp, replacement: string): string` | First match replaced, or all when the RegExp is global; a plain string matches literally |
| `replaceAll` | `replaceAll(search: string \| RegExp, replacement: string): string` | Every occurrence; a non-global RegExp is a runtime `TypeError` |
| `matchAll` | `matchAll(regexp: RegExp): string[][]` | Every match of a **global** RegExp as an `[full, group1, …]` array (the `exec` shape); scan stops after the first empty match; requires the `g` flag |

There is no `s.match(regex)` or `s.search(regex)` — use the RegExp instance's `match`/`test` or the methods above. `matchAll`, `replace` and `replaceAll` signatures are verified against the compiler's string-surface tables (`src/std/string`); the guide's regex page does not enumerate them. Regex behavior is governed by the vendored PCRE2 engine, not the host; avoid ambiguous nested quantifiers on untrusted input and measure adversarial cases.

Behavior details: [guide](../../standard-library/17-regular-expressions.md)
