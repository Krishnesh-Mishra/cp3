# date-format - format 50,000 UTC instants by hand

Date/time workload, identical in all five participating languages. Construct
one instant from the fixed epoch millisecond value `1700000000000`
(`2023-11-14T22:13:20Z`), add `k % 3600` seconds for iteration `k`, and build
the UTC spelling manually:

```text
YYYY-MM-DDTHH:MM:SS
```

The formatted string is exactly 19 ASCII characters. Fold each character with
the fixed weights `0`-`9` -> 0-9, `-` -> 10, `T` -> 11, `:` -> 12, then add
the string length and the fixed difference between the two dates
(`1700000123456 - 1700000000000 = 123456`):

```text
checksum = (checksum * 31 + char_sum + 19 + 123456) % 1000003
```

One line, one integer: `796897`.

The date is never formatted through a locale-aware API. Every entry formats
the numeric UTC fields itself, so the output is machine- and locale-independent.

## Language notes

- cpp: `std::chrono::system_clock` + `gmtime_s` / `gmtime_r` + `snprintf`.
- go: `time.UnixMilli(...).UTC()` + `fmt.Sprintf`.
- js: `new Date(ms)` + `getUTC*` getters + `padStart`.
- python: `datetime.fromtimestamp(..., timezone.utc)` + f-string fields.
- cp3: `std/date` `Date.fromISOString` / `addSeconds` / `diffMs`; the landed
  `getFullYear`/`getMonth`/... getters are documented as UTC, so they match
  the other entries without a timezone conversion.
- rust: **omitted** - Rust `std` has no timezone-aware date formatter and no
  calendar-field API; adding `chrono` would compare a dependency, not `std`.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O2 -o date-format.exe main.cpp
./date-format.exe

# go
go build -o date-format.exe main.go
./date-format.exe

# js
node main.mjs

# python
python main.py

# cp3
build/bin/cp3c.exe build benchmarks/date-format/cp3/main.cp3
build/bin/cp3c.exe run benchmarks/date-format/cp3/main.cp3
```

The oracle value is re-derived by `../bench/oracle.py`
(`date_format_checksum`):

```bash
python benchmarks/bench/oracle.py
```

Rerun the tier with `python benchmarks/run_benchmark.py --only date-format`.
