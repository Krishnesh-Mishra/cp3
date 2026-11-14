---
title: Strings
---
# Strings

`string` is a built-in immutable text type. CP3 stores its contents as UTF-8 bytes. That representation matters: `.length` reports bytes, while `charAt` and string `for...of` walk UTF-8 code points. Neither measure counts user-perceived grapheme clusters.

```ts
const greeting = "hé😀";
console.log(greeting.length); // UTF-8 byte count
console.log(greeting.charAt(1)); // one complete code point
```

The emoji occupies multiple bytes but one code point. A displayed symbol can also consist of multiple code points, such as a base character followed by combining marks. CP3's code-point operations do not perform grapheme segmentation or Unicode normalization.

## Byte length and indexing

`text.length` is the UTF-8 storage size in bytes. `text[index]` is byte-oriented and returns a numeric byte value; it is not the same operation as `text.charAt(index)`.

`charAt(index)` and `at(index)` return a whole UTF-8 code point as a string. The index is a code-point position. An out-of-range `charAt` returns an empty string. For a negative or non-finite index, follow the current runtime implementation and tests rather than assuming full ECMAScript `String.prototype.at` behavior; CP3's `at` is currently lowered to the same operation as `charAt`.

`charCodeAt(index)` returns the raw UTF-8 byte at byte position `index` as a number, the same value as `text[index]`. An out-of-range or non-integer index returns `NaN`. `String.fromCharCode(...codes)` is the byte-oriented inverse: each code is reduced modulo 256 and appended as one raw UTF-8 byte, so `String.fromCharCode(text.charCodeAt(i))` reproduces the byte at `i`. With no arguments it returns the empty string.

## Code points and Unicode scalars

`text.codePointAt(index)` returns the Unicode scalar value of the code point at code-point position `index` — the unit `charAt` counts, not `text[index]`'s bytes and not JavaScript's UTF-16 code units. An out-of-range or non-integer position returns `NaN`. `String.fromCodePoint(...codes)` is the inverse: it UTF-8 encodes each Unicode scalar code. A code outside `0..0x10FFFF`, or a UTF-16 surrogate code point, is a hard `RangeError` — it prints `cp3: RangeError: ...` to stderr and exits the process; it is not a catchable error. Fractional codes truncate toward zero. With no arguments it returns the empty string.

```ts
"hé😀".codePointAt(1);       // 233 (é)
String.fromCodePoint(233);   // "é"
String.fromCodePoint(0x1F600); // "😀"
```

These do not perform grapheme segmentation or normalization; a combining mark is still its own code point.

## Iterating text

String `for...of` visits complete UTF-8 code points in order:

```ts
const word = "A😀B";
for (const character of word) {
  console.log(character);
}
```

This is different from indexing the string by byte offset. It is also not grapheme-aware: a combining mark is a separate code point even if a terminal or font renders it together with the preceding character.

Use byte operations for protocol formats that are defined in bytes. Use code-point iteration for text operations that need to avoid splitting a well-formed UTF-8 sequence. Validate inputs at system boundaries if malformed byte sequences can enter through native or binary APIs.

## Searching

| Method | Meaning |
| --- | --- |
| `includes(search)` | True if the byte sequence occurs in the string. |
| `startsWith(search)` | True if the string begins with the supplied text. |
| `endsWith(search)` | True if it ends with the supplied text. |
| `indexOf(search, fromIndex?)` | Returns a byte position for the first match or `-1`. |
| `lastIndexOf(search, fromIndex?)` | Returns a byte position for the last match or `-1`. |

Search results are positions in the UTF-8 string storage. For ASCII, byte positions, code-point positions, and character positions often coincide; for multibyte text they can differ. Do not use a search result as a code-point index without converting it.

These functions perform substring search. They do not tokenize source code, interpret locale-specific collation, normalize Unicode, or automatically treat a plain search string as a regular expression.

## Matching all occurrences

`text.matchAll(regexp)` returns every match of a global (`g`) regexp as an array of match arrays: each inner array is `[full, group1, ...]`, the same shape `RegExp.exec` produces. The result is an eager `string[][]` — CP3 has no lazy iterator here. A non-global regexp is a hard `TypeError` (prints to stderr and exits; not catchable). The scan runs on a fresh clone of the regexp, so a shared `g` regexp's position is not disturbed, and a sticky (`y`) regexp is honored; the scan stops after the first empty match.

```ts
const matches = "a12b34".matchAll(/[0-9]+/g);
// [["12"], ["34"]]
```

See [Regular expressions](../../standard-library/17-regular-expressions.md) for flags, capture groups, and the cursor semantics of stateful patterns.

## Extracting substrings

`slice(start?, end?)` and `substring(start?, end?)` select ranges using byte-oriented positions. `slice` supports negative positions relative to the end; `substring` clamps negative positions to zero and swaps the endpoints when the start exceeds the end.

```ts
const value = "abcdef";
console.log(value.slice(1, 4));      // bcd
console.log(value.substring(4, 1)); // bcd
```

On multibyte strings, a byte boundary can fall inside an encoded code point. These APIs use the runtime's byte-range semantics; they do not promise to adjust every boundary to a Unicode character boundary. Use `charAt` or code-point iteration when processing text by code point.

## Replacing and splitting

`replace(search, replacement)` replaces the first matching occurrence. `replaceAll(search, replacement)` replaces all occurrences. Both return new strings and leave the source value unchanged. `search` may be a plain string (matched literally) or a `RegExp` (a global regex replaces every occurrence for `replace` and is required for `replaceAll`; a non-global regex passed to `replaceAll` is a runtime TypeError). Replacement text is inserted literally — `$` is not a group-reference marker. See [Regular expressions](../../standard-library/17-regular-expressions.md) for pattern construction and match semantics.

`split(separator)` accepts a string separator or a `RegExp` according to the checker and code generator. A string separator is matched literally. A regular expression uses CP3's regular-expression runtime. Splitting constructs an array containing the pieces, so account for its memory cost on large inputs; see [Regular expressions](../../standard-library/17-regular-expressions.md) for captures, state, and Unicode differences.

```ts
const fields = "one,two,three".split(",");
const words = "red   blue".split(/\s+/);
```

An empty or non-matching separator follows the runtime implementation and test corpus; do not assume every corner case matches a browser engine. See [Regular expressions](../../standard-library/00-overview.md) only if the module guide lists it in the current build; regex values are also covered by the language tests.

## Trimming and case conversion

`trim`, `trimStart`, and `trimEnd` return a copy with surrounding ASCII whitespace removed. The current implementation recognizes space, tab, line feed, vertical tab, form feed, and carriage return. It does not use a complete Unicode whitespace property table.

`toUpperCase` and `toLowerCase` perform ASCII case mapping. Bytes outside ASCII, including the bytes that encode UTF-8 non-ASCII code points, pass through unchanged. These functions are convenient for ASCII protocol tokens; they are not full locale-aware Unicode case folding and should not be used alone for security-sensitive identifier normalization.

## Padding and repetition

`repeat(count)` returns repeated text. The current runtime truncates the count and clamps negative values to zero; it does not currently match JavaScript's `RangeError` behavior for every invalid count. Avoid untrusted counts that could request huge allocations.

`padStart(targetLength, fill?)` and `padEnd(targetLength, fill?)` add fill text when the current string is shorter than the target. The default fill is a space. The implementation's length unit is the stored string length, so UTF-8 byte length affects the result; a multi-byte fill string can also add more than one byte per repetition.

```ts
const id = "5".padStart(3, "0"); // "005"
const label = "cp3".padEnd(6, ".");
```

If the requested target is already met, the original contents are returned unchanged. Validate target sizes when input comes from an untrusted source.

## Escape-free text: `String.raw`

`String.raw(chunk)` returns its one-argument chunk unchanged — no escape processing is applied. The two-parameter form `raw(parts, ...substitutions)` joins the `parts` chunks with the interpolated substitutions between them: `raw(["a", "b"], "x")` returns `axb`. CP3 has no tagged templates, so this is the spelling of the raw-string helper as an ordinary call; each form takes the arguments in the order given.

```ts
String.raw("C:\\new\\tmp");    // identity: returned unchanged
String.raw(["a", "b"], "x");   // "axb"
```

`raw` itself performs no escape processing; the interpolated substitutions are inserted verbatim between the chunks.

## Concatenation and cost

The `+` operator combines strings — `string + string` is the one mixed-shape rule: `"a" + 1` and `"x" + true` are compile errors (`CP3411`), as is adding a string to a number. Interpolate with template literals, or convert explicitly, when you need to build text from non-string values:

```ts
import console from "std/console";

const n: number = 3;
console.log(`${n} items`);    // template literal
console.log("count: " + String(n)); // explicit conversion
```

Transformations such as `replaceAll`, `slice`, `split`, case conversion, padding, and repetition create result storage. When processing a large text stream, avoid retaining every intermediate result if a direct scan or streaming API is available.

## Immutability and sharing

Strings behave as immutable values at the language level. A method returns a new string instead of modifying the original:

```ts
const raw = "  CP3  ";
const cleaned = raw.trim();
console.log(raw);     // still has surrounding spaces
console.log(cleaned); // trimmed result
```

Assigning a string to another binding does not create mutable shared text. Reassigning a `let` name changes that binding; it does not alter another string value already held elsewhere.

## Method surface

The documented checker surface includes `length`, `charAt`, `at`, `charCodeAt`, `codePointAt`, `matchAll`, `startsWith`, `endsWith`, `includes`, `repeat`, `padStart`, `padEnd`, `toUpperCase`, `toLowerCase`, `trim`, `trimStart`, `trimEnd`, `indexOf`, `lastIndexOf`, `slice`, `substring`, `replace`, `replaceAll`, and `split`. The static `String.fromCharCode(...codes)` constructs a byte string from numeric codes; `String.fromCodePoint(...codes)` constructs one from Unicode scalar values; `String.raw` is the ordinary-call spelling of the raw-string helper. The `tests/cases/arrays-strings/02/str` corpus pins the runtime behavior.

The method names intentionally resemble JavaScript, but the byte and code-point distinctions above are CP3's current behavior; the conformance fixtures under `tests/cases/arrays-strings` pin it.

## Related pages

- [Arrays](./04-arrays.md)
- [Byte buffers](../../standard-library/11-buffer.md)
- [Typed arrays and DataView](../../standard-library/16-typed-arrays.md)
- [Functions and closures](../02-functions/01-functions-and-closures.md)
