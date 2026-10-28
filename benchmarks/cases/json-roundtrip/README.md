# json-roundtrip - 20,000 parse + stringify round trips of one record

Parse one fixed 310-byte JSON object, stringify the same fields back, and fold
the field values plus the payload and output lengths into one integer:

```
664346
```

The payload is one representative API record with 11 fields: `id` (number),
`name`, `email` (strings), `active` (boolean), `score` (number), `title`,
`company`, `city`, `country` (strings), `tags` (`string[]`, 6 entries) and
`samples` (`number[]`, 10 entries). It is ASCII-only, so character counts and
byte counts agree in every language.

Each of the 20,000 iterations does exactly this:

1. parse `TEXT` into the record;
2. stringify the same 11 fields back out;
3. compute
   `fieldValue = id + len(name) + len(email) + (active ? 1 : 0)
   + int(score * 10) + len(title) + len(company) + len(city) + len(country)
   + len(tags) + len(samples) + sum(len(tag)) + sum(samples)`;
4. `checksum = (checksum * 31 + fieldValue + 310 + len(out)) % 1000003`.

`len(out)` is part of the fold, so the serializer cannot be optimized away: a
language that formats the text differently prints a different number. `310` is
the payload length and is also folded so the parse half cannot be skipped.

## Expected output

```
664346
```

Re-derived independently by `../../runner/oracle.py` (`json_roundtrip_checksum`):

```bash
python benchmarks/runner/oracle.py
# json payload bytes                = 310
# json_roundtrip_checksum()         = 664346
```

## Entrants, and why cpp/rust sit this row out

Entries: cp3, Go, JavaScript (Node) and Python. C++ and Rust are deliberately
omitted. Neither standard library has a JSON parser/serializer: C++ has none at
all, and Rust has none in `std` without a crate. A single dependency-free file
would have to hand-write one, which measures that hand-written code rather than
the language's JSON stack; vendoring nlohmann/json or adding serde_json would
measure a dependency stack instead. Go's `encoding/json`, Node's `JSON`, and
Python's `json` are standard-library surfaces, so those rows compare like with
like. The runner prints the omission reason and still gates cp3 against
go/js/python.

## Workload shape and the cp3 landed surface

All four entries use the same payload shape. cp3 cannot round-trip a nested
object or an array of records yet: `interface` record shapes accept only
primitives and one-level primitive arrays (`src/std/json/codegen.cpp`,
`recordFields()` / `recordFieldOf()`), and `JSON.stringify` lowers an
object-shaped value only through an object literal (`emitJsonStringify()`
rejects the rest with CP3002). The payload therefore uses scalars plus
`string[]` and `number[]`, which round-trip today. The wave report
(`build/scratch/rounds/WAVE-BENCH-JSON-ASYNC-REPORT.md`) ranks nested objects
and arrays of records as a JSON gap; the entries can adopt them unchanged once
the surface accepts them.

Fairness notes:

- Go marshals a struct. Its float encoder writes `93.5` as `93.5`, and struct
  field order matches the payload order, so the stringified text is
  byte-identical to the input.
- Python uses `json.dumps(..., separators=(",", ":"), ensure_ascii=True)` and
  folds `int(score * 10)`, so the printed value is an integer.
- JS uses `JSON.parse`, then `JSON.stringify` of an object literal.
- cp3 uses `JSON.parse<User>(TEXT)` and `JSON.stringify({...})` of an object
  literal, and folds `out.length` like every other entry.

## Run one by hand

From each language's folder:

```bash
# go
go build -o json-roundtrip.exe main.go
./json-roundtrip.exe

# js
node main.mjs

# python
python main.py

# cp3 (build/bin/cp3c.exe is the compiler at the repo root)
cd benchmarks/cases/json-roundtrip/cp3
../../../../build/bin/cp3c.exe build src/index.cp3
./.cp3/out/index.exe
```

Rerun the row through the suite with `python benchmarks/runner/run_benchmark.py
--only json-roundtrip --reps 3` and compare the `output` and `match` columns.
