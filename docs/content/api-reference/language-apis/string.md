---
title: String
---
# String

`string` is CP3's built-in immutable text type; values are stored as UTF-8 bytes. Methods return new strings. See the [strings guide](../../language/04-values-and-objects/05-strings.md) for the byte vs code-point model in depth.

## Length and representation

| Member | Signature | Notes |
| --- | --- | --- |
| `length` | `length: number` | UTF-8 **byte** count, not code-point or grapheme count. |
| `text[index]` | `number` | Raw UTF-8 byte value at byte offset `index` (same as `charCodeAt`). |
| `charAt` | `charAt(index: number): string` | Whole UTF-8 code point at code-point position; out-of-range returns `""`. |
| `at` | `at(index: number): string` | Currently lowered to the same operation as `charAt`; do not assume ECMAScript `String.prototype.at` semantics for negative indices. |
| `charCodeAt` | `charCodeAt(index: number): number` | Raw UTF-8 byte at byte position `index`; out-of-range or non-integer returns `NaN`. |
| `codePointAt` | `codePointAt(index: number): number` | Unicode scalar value at code-point position; out-of-range or non-integer returns `NaN`. |

String `for...of` visits whole code points. No operation performs grapheme segmentation or Unicode normalization; a combining mark is its own code point. Byte search results are byte positions — do not use them as code-point indices without converting.

## Statics and coercion

| Member | Signature | Notes |
| --- | --- | --- |
| `String.fromCharCode` | `String.fromCharCode(...codes: number[]): string` | Byte-oriented inverse of `charCodeAt`; each code is reduced modulo 256 and appended as one raw UTF-8 byte. No arguments → `""`. |
| `String.fromCodePoint` | `String.fromCodePoint(...codes: number[]): string` | UTF-8 encodes each Unicode scalar code. Codes outside `0..0x10FFFF` or UTF-16 surrogates are a hard `RangeError` (prints `cp3: RangeError: ...` and exits; not catchable). Fractional codes truncate toward zero. No arguments → `""`. |
| `String.raw` | `String.raw(chunk: string): string` / `String.raw(parts: string[], ...substitutions: string[]): string` | The raw-string helper as an ordinary call (CP3 has no tagged templates). One-argument form returns its argument unchanged; two-argument form joins `parts` with the substitutions verbatim (`raw(["a", "b"], "x")` → `"axb"`). No escape processing is applied. |
| `String()` | `String(value): string` | Coercion global; argument unconstrained; lowers onto `toString`. |

## Searching

| Method | Signature | Notes |
| --- | --- | --- |
| `includes` | `includes(search: string): boolean` | True when the byte sequence occurs. |
| `startsWith` | `startsWith(search: string): boolean` | True when the string begins with `search`. |
| `endsWith` | `endsWith(search: string): boolean` | True when the string ends with `search`. |
| `indexOf` | `indexOf(search: string, fromIndex?: number): number` | Byte position of first match, or `-1`. |
| `lastIndexOf` | `lastIndexOf(search: string, fromIndex?: number): number` | Byte position of last match, or `-1`. |
| `matchAll` | `matchAll(regexp: RegExp): string[][]` | Eager array of match arrays (`[full, group1, ...]`), same shape as `RegExp.exec`. Requires a global (`g`) regexp — a non-global regexp is a hard `TypeError` (exits). Scan runs on a fresh clone; empty matches stop the scan. See [regular expressions](../../standard-library/17-regular-expressions.md). |

Search is literal substring matching; it does not tokenize, collate, normalize, or treat the search string as a pattern.

## Extracting substrings

| Method | Signature | Notes |
| --- | --- | --- |
| `slice` | `slice(start?: number, end?: number): string` | Byte-oriented range; negative positions count from the end. |
| `substring` | `substring(start?: number, end?: number): string` | Byte-oriented range; negative positions clamp to 0 and swapped endpoints are exchanged. |

On multibyte text a byte boundary can fall inside an encoded code point; these APIs do not promise to adjust boundaries to code points. Use `charAt` or code-point iteration instead.

## Replacing and splitting

| Method | Signature | Notes |
| --- | --- | --- |
| `replace` | `replace(search: string \| RegExp, replacement: string): string` | Replaces the first matching occurrence; a global regexp replaces every occurrence. |
| `replaceAll` | `replaceAll(search: string \| RegExp, replacement: string): string` | Replaces every occurrence; a non-global regexp is a runtime `TypeError`. |
| `split` | `split(separator: string \| RegExp): string[]` | String separators match literally; a `RegExp` uses CP3's regexp runtime. |

Both replace methods return new strings; the source is unchanged. Replacement text is inserted literally — `$` is not a group-reference marker. See [regular expressions](../../standard-library/17-regular-expressions.md) for match semantics.

## Trimming, case, padding, repetition

| Method | Signature | Notes |
| --- | --- | --- |
| `trim` | `trim(): string` | Removes surrounding ASCII whitespace (space, tab, LF, VT, FF, CR); no full Unicode whitespace table. |
| `trimStart` | `trimStart(): string` | Leading ASCII whitespace removed. |
| `trimEnd` | `trimEnd(): string` | Trailing ASCII whitespace removed. |
| `toUpperCase` | `toUpperCase(): string` | ASCII case mapping only; non-ASCII UTF-8 bytes pass through unchanged. Not locale-aware. |
| `toLowerCase` | `toLowerCase(): string` | ASCII case mapping only. |
| `repeat` | `repeat(count: number): string` | Count is truncated and negative values clamp to zero; avoid untrusted counts. |
| `padStart` | `padStart(targetLength: number, fill?: string): string` | Pads on the left; default fill is a space. Length unit is the stored (byte) length. |
| `padEnd` | `padEnd(targetLength: number, fill?: string): string` | Pads on the right; default fill is a space. |

## Concatenation and `+`

| Rule | Detail |
| --- | --- |
| `string + string` | String concatenation. |
| `string + non-string` | Compile error `CP3411` — e.g. `"a" + 1`, `"x" + true`. Interpolate with template literals or convert explicitly: `` `${n}` `` or `"count: " + String(n)`. |

## Template literals

Template literals (`` `...${expr}...` ``) interpolate any expression; each interpolated expression is stringified with the runtime formatter. There are no tagged templates. `` `${n}` `` is the preferred spelling when a string may hold a non-string value. See [types](../../language/01-variables-and-bindings/02-types.md) for the grammar.

## Related pages

- [Arrays](../../language/04-values-and-objects/04-arrays.md)
- [Global functions](./global-functions.md) — `String`, `atob`, `btoa`, `structuredClone`
- [Regular expressions](../../standard-library/17-regular-expressions.md)
