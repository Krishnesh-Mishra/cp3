---
title: Strings
chapter: Chapter 3 · Standard library
---
# Strings

Strings are built-in immutable text values. CP3 stores text as UTF-8. A string's `length` is its encoded byte length, not necessarily the number of user-perceived characters.

```ts
const greeting: string = "नमस्ते";
console.log(greeting.length); // UTF-8 bytes
```

Do not use `length` as a grapheme count for international text. A visible character can contain multiple Unicode code points, and each code point may occupy multiple UTF-8 bytes.

## Reading characters and bytes

- `charAt(index)` returns the character at the supported position.
- `at(index)` supports the indexed access form provided by the runtime.
- `charCodeAt(index)` returns the byte value at the UTF-8 offset.

The byte-oriented `charCodeAt` result is not a Unicode scalar value for multibyte text. Use the operation appropriate to the representation you need.

## Searching and matching

`includes`, `startsWith`, and `endsWith` test for text. `indexOf` and `lastIndexOf` return a position or `-1`. These operations search string contents; they do not parse a language grammar or normalize Unicode.

## Extracting and replacing

- `slice(start, end?)` extracts a range.
- `substring(start, end?)` extracts a range using substring-style index handling.
- `replace(search, replacement)` replaces the first matching occurrence.
- `replaceAll(search, replacement)` replaces all matching occurrences.
- `split(separator)` returns an array of pieces.

Use `split` with care on large strings because it creates an array of all parts. A separator is literal according to the supported string API; do not expect JavaScript regular-expression behavior unless the type signature explicitly accepts a regex.

## Whitespace and case

`trim`, `trimStart`, and `trimEnd` remove surrounding whitespace. `toUpperCase` and `toLowerCase` return converted strings. Case conversion can depend on Unicode rules and should not be used as a security-sensitive identifier normalization strategy.

## Padding and repetition

`repeat(count)` repeats the string. `padStart(targetLength, fill?)` and `padEnd(targetLength, fill?)` add fill text to reach a target length according to the runtime's string length measure. Avoid very large counts or target sizes from untrusted input.

## Immutability

String methods that appear to modify text return new strings; they do not mutate the original value:

```ts
const name = "  CP3  ";
const clean = name.trim();
```

## Method inventory

`length`, `charAt`, `at`, `charCodeAt`, `startsWith`, `endsWith`, `includes`, `repeat`, `padStart`, `padEnd`, `toUpperCase`, `toLowerCase`, `trim`, `trimStart`, `trimEnd`, `indexOf`, `lastIndexOf`, `slice`, `substring`, `replace`, `replaceAll`, and `split` make up the documented string surface. Some names resemble JavaScript methods but use CP3's byte-based string representation and signatures.
