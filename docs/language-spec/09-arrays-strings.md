# 9. Arrays & Strings

JS-like API, strongly typed. Backed by `std::vector<T>`:

```cp3
let numbers: number[] = [1, 2, 3];
numbers.push(4);
numbers.pop();
let x = numbers[0];

numbers.push("hello"); // compile error - type mismatch
```

Allocation, resizing, copying, and destruction are all automatic.

## Arrays are homogeneous by default - mixed types require an explicit union

Because arrays are backed by `std::vector<T>` - which can only ever hold one concrete type - **arrays cannot silently mix types just because the underlying container "is a vector."** A plain array's element type is fixed:

```cp3
let numbers: number[] = [1, 2, 3];
numbers.push("hello"); // compile error - type mismatch, number[] holds only numbers
```

If mixed types are genuinely needed, the type itself must say so explicitly, using the union mechanism already defined in [§3](03-type-system.md):

```cp3
let mixed: (number | string)[] = [1, "two", 3];
```

This compiles to `std::vector<std::variant<double, std::string>>` - still statically typed, still no `any`, still no dynamic boxing. **A fully dynamic array (anything goes, no declared type at all) is not supported** - allowing that would reintroduce the same dynamic-typing cost (a boxed/`any`-style runtime value on every element) that the whole "no `any`" design goal exists to avoid.

## Array and string methods, made explicit rather than left as "etc."

Earlier drafts of this spec described array/string methods with an illustrative list ending in "etc.," which leaves an implementer guessing exactly where the line is. The full list, each backed by a direct C++ STL/C++20/bundled-library equivalent - no custom parsing logic needed anywhere in this list:

**Array**, beyond the already-shown `push`/`pop`/`map`/`filter`/`reduce`/`find`/`includes`/`flat`:

| Method | Backed by |
|---|---|
| `sort(comparator?)` | `std::stable_sort` - see the two decisions below |
| `join(sep)` | `fmt::join` (`fmt` is already bundled for `console.log`, [CLI-README.md §3](../cli-tooling/03-internal-library-wrapping.md)) |
| `indexOf(x)` | `std::find` |
| `findIndex(fn)` | `std::find_if` |
| `some(fn)` | `std::any_of` |
| `every(fn)` | `std::all_of` |
| `unique()` | `std::unique` - mutates in place, removes adjacent duplicates |
| `shuffle()` | `std::shuffle` with a seeded RNG - mutates in place |
| `binarySearch(target)` | `std::lower_bound`. **Precondition: the array must already be sorted** - this is a genuine, real footgun carried over from C++ itself (an unsorted array gives a wrong answer, not an error), so a debug build adds a sorted-check that panics with a clear message; a release build skips the check for speed, same debug/release split as array bounds-checking |

**`sort()`, two decisions made explicit rather than silently inheriting JS's own behavior:**

1. **No comparator → type-aware default, not JS's string-coercion default.** JS's own `[10, 9, 2].sort()` is a famous footgun - it sorts by *string* conversion, giving `[10, 2, 9]`. Since cp3 arrays are typed, there's no reason to inherit that: `sort()` with no comparator on a `number[]` sorts numerically, and on a `string[]` sorts lexicographically. A deliberate, named departure from JS, same spirit as UTF-8 strings and immutable `Date` - document it, don't silently diverge.
2. **Stable, not just fast.** `std::sort` isn't guaranteed stable; `std::stable_sort` is. Backing `sort()` with `std::stable_sort` matches JS's own (stable) sort semantics exactly, at a small, worthwhile performance cost.

**A comparator returns a JS-style number (negative/zero/positive), not a C++-style bool** - the compiler wraps the user's comparator, comparing its result against `0`, so `nums.sort((a, b) => a - b)` works exactly as a JS developer expects without needing to know C++ comparators return `bool`.

**String**, beyond the already-shown `split`/`trim`/`replace`/`padStart`/`padEnd`/`toUpperCase`/`toLowerCase`/`charAt`:

| Method | Backed by |
|---|---|
| `startsWith(s)` | `std::string::starts_with` - native to C++20, zero extra work |
| `endsWith(s)` | `std::string::ends_with` - native to C++20, zero extra work |
| `includes(s)` | `std::string::find` |
| `indexOf(s)` | `std::string::find` |
| `repeat(n)` | a simple loop/reserve, no library needed |
| `slice(a, b)` / `substring(a, b)` | `std::string::substr` |
| `replaceAll(a, b)` | `absl::StrReplaceAll` (Abseil, [CLI-README.md §3](../cli-tooling/03-internal-library-wrapping.md)) |
| `trimStart()` / `trimEnd()` | `absl::StripLeadingAsciiWhitespace` / `absl::StripTrailingAsciiWhitespace` (Abseil) |

## Strings are UTF-8, not UTF-16 - a deliberate, documented departure from JS

JS strings are UTF-16 internally; cp3 strings are **UTF-8**, backed by `std::string` directly. This is a real, visible difference from JS, not an implementation detail to gloss over, and it has to be stated plainly rather than discovered by surprise:

- **`str[i]` returns a raw byte, not a character.** In UTF-8, a character can take anywhere from 1 to 4 bytes - an emoji like `"😀"` is 4 bytes on its own. Indexing by byte position means `str[0]` on a string starting with a multi-byte character returns one raw byte of that character, not the character itself - silently different from JS, where `"😀"[0]` at least returns half a UTF-16 surrogate pair (also not the full character, but a different kind of "wrong"). Neither language gives you a full character from raw indexing once you're outside plain ASCII - cp3 is just explicit about it being bytes.
- **`str.charAt(i)` returns the actual visual character**, reusing JS's own existing method name rather than inventing a new one. Internally this has to walk the string respecting UTF-8's variable-width encoding (checking the leading byte to know how many bytes the character actually spans) rather than jumping directly to a byte offset - it cannot be a simple O(1) array lookup the way `str[i]` is.
- **Why UTF-8 over UTF-16:** half the memory footprint for the common case (ASCII/Latin text), and 1:1 compatibility with `std::string` and every native C/C++ API a program might interop with ([§19](19-c-cpp-interoperability.md)) - a UTF-16 string would need a full re-encoding at every native boundary crossing. This is the same choice Rust and Go both made for their own default string types, for the same reasons.

