# json-parse-large - parse a 200 KiB JSON record

Large-payload JSON workload. Every language builds the same JSON text in
memory, parses it, folds the parsed fields into one integer, and prints it.
Unlike `json-roundtrip`, this row does not stringify: it measures the parser
at a realistic payload size.

## Workload

Build this exact compact JSON text (no whitespace between tokens):

```text
{"id":48271,"name":"Ada Lovelace","active":true,"score":93.5,
 "numbers":[n0,n1,...,n49999],
 "tags":["tag-0","tag-1",...,"tag-999"]}
```

The line breaks above are explanatory; the actual text is one line with no
spaces. The field values are:

- `id = 48271`
- `name = "Ada Lovelace"`
- `active = true`
- `score = 93.5`
- `numbers` has `50000` elements, where `numbers[i] = (i * 17 + 3) % 1000`,
  printed as a plain base-10 integer.
- `tags` has `1000` elements, where `tags[i] = "tag-" + i`.

The resulting text is exactly `204472` bytes. Constants:

```text
NUMBERS    = 50000
TAGS       = 1000
ITERATIONS = 20
MOD        = 1000003
```

For each of `ITERATIONS` iterations, parse the text and compute:

```text
field = id
      + length(name)
      + (active ? 1 : 0)
      + trunc(score * 10)
      + length(numbers)
      + length(tags)
      + sum(numbers)
      + sum(length(tag) for tag in tags)

checksum = (checksum * 31 + field + length(TEXT)) % MOD
```

`checksum` starts at `0`, and `field` is `25082109`. The text length is a
constant because every entry builds the same bytes.

One line, one integer: `613781`.

## Why this task

`json-roundtrip` uses a 310-byte record and pays serializer cost on every
iteration. This row isolates the parser and scales the payload to 200 KiB, so
array decoding, number parsing and allocation growth dominate. It is the
large-payload counterpart to the small round-trip row.

## Language notes

- go: `encoding/json` into a struct with `Numbers []int` and `Tags []string`.
- js: `JSON.parse(text)`.
- python: `json.loads(text)`.
- java: Java's standard library has no JSON parser. Like the existing
  `json-roundtrip` Java entry, this entry carries a small recursive-descent
  parser for the fixed shape; no third-party library is used.
- cp3: `JSON.parse<Payload>(TEXT)` with:

  ```cp3
  interface Payload {
    id: number;
    name: string;
    active: boolean;
    score: number;
    numbers: number[];
    tags: string[];
  }
  ```

  Use `typeSystem: "rust"` in `package.json`. Build the text with arrays of
  strings joined by commas rather than repeated `+=` concatenation in the
  hot construction path.

cpp and rust are omitted: neither standard library has a JSON parser, and
vendoring one would compare dependencies rather than the language. This is the
same omission rule the existing JSON row uses.

## Run one by hand

From each language's folder:

```bash
# go
go build -o json-parse-large.exe main.go
./json-parse-large.exe

# js
node main.mjs

# python
python main.py

# java
javac -d . Main.java
java Main

# cp3
build/bin/cp3c.exe build benchmarks/cases/json-parse-large/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/json-parse-large/cp3/src/index.cp3
```

The value is re-derived independently by `../../runner/oracle.py`
(`json_parse_large_checksum`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the row with `python benchmarks/runner/run_benchmark.py --only json-parse-large`.
