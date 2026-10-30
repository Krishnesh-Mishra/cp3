# regex-scan - global numeric regex scan over generated text

Regular-expression workload over a fixed ASCII text. Every language runs the
same global pattern, counts the matches, sums their numeric values, and prints
one integer.

## Workload

The source unit is exactly:

```text
alpha 123 beta 45 gamma 6789 delta 0 epsilon 42\n
```

`\n` is one newline byte. Build the text by repeating the unit exactly
`REPEATS = 5000` times. The resulting text is `240000` bytes.

Constants:

```text
REPEATS = 5000
ROUNDS  = 8
MOD     = 1000000007
PATTERN = /[0-9]+/g
```

For each of `ROUNDS` rounds:

1. Run the global pattern over the text and collect every match.
2. Count the matches (`count`).
3. Parse each matched ASCII digit run as a base-10 integer and sum them
   (`sum`). A hand-written digit loop or the language's standard integer parse
   is fine; all values are small.
4. Fold the round into the checksum:

```text
checksum = (checksum * 31 + count + sum) % MOD
```

`checksum` starts at `0`. Each round sees the same text, so `count` is
`25000` and `sum` is `34995000` every round.

One line, one integer: `735703090`.

## Why this task

It measures the regex engine's scan and match-allocation path in a hot loop,
not a one-shot match. The text is large enough that per-match overhead and
substring allocation matter, and the numeric fold makes the match count and
values observable.

## Language notes

- cpp: `std::regex` with `std::sregex_iterator` (standard library only).
- go: `regexp.MustCompile(`[0-9]+`)` and `FindAllString`.
- js: `text.match(/[0-9]+/g)` (or `RegExp.exec` in a loop).
- python: `re.findall(r"[0-9]+", text)`.
- java: `Pattern.compile("[0-9]+")` and a `Matcher` loop.
- cp3: the built-in regex literal, e.g. `/[0-9]+/g.match(TEXT)`; iterate the
  resulting match array. Use `typeSystem: "rust"` in `package.json`.

Rust is omitted: Rust's standard library has no regular-expression engine.
Adding the `regex` crate would compare third-party dependencies, not the
language's standard surface, which is the same rule the suite applies to JSON,
timers and SHA-256.

Every entry must run the global match over the whole text each round. Do not
hoist the matches out of the round loop; the repeated scan is the workload.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O3 -o regex-scan.exe main.cpp
./regex-scan.exe

# go
go build -o regex-scan.exe main.go
./regex-scan.exe

# js
node main.mjs

# python
python main.py

# java
javac -d . Main.java
java Main

# cp3
build/bin/cp3c.exe build benchmarks/cases/regex-scan/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/regex-scan/cp3/src/index.cp3
```

The value is re-derived independently by `../../runner/oracle.py`
(`regex_scan_checksum`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the row with `python benchmarks/runner/run_benchmark.py --only regex-scan`.
