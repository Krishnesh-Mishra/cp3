# sha256-churn - SHA-256 over one fixed 1 KiB buffer, 60,000 times

Hash-heavy workload, identical in every participating language. Build a
1,024-byte buffer by repeating the 16-byte seed `0123456789abcdef`, then
hash that same buffer 60,000 times with the platform's SHA-256. After each
hash, read the first four digest bytes as one big-endian 32-bit integer and
fold it into the running checksum:

```
checksum = (checksum * 31 + first_four_digest_bytes) % 1000003
```

One line, one integer: `218229`.

The buffer never changes between iterations, so the measured work is the hash
implementation itself plus the per-call boundary (digest allocation and the
first-dword read). Sixty thousand hashes keep the row in the 100-500 ms band
on the suite's reference machine; the value is a fixed source constant in every
entry, so runs are deterministic and integer-exact.

## Language entries

| language | SHA-256 surface | digest read |
| --- | --- | --- |
| cp3 | `std/crypto` `crypto.sha256(data)` | hex string, first 8 hex digits through `parseInt("0x" + ...)` |
| go | `crypto/sha256` `sha256.Sum256` | `encoding/binary` big-endian `Uint32` of `sum[:4]` |
| js | `node:crypto` one-shot `hash("sha256", data)` | `parseInt` of the first 8 hex digits |
| python | `hashlib.sha256(data).digest()` | `int.from_bytes(digest[:4], "big")` |
| cpp | **omitted** | no SHA-256 in the C++ standard library |
| rust | **omitted** | no SHA-256 in Rust `std` without a crate |

The cp3 entry passes the `Buffer` from `std/buffer` straight to
`crypto.sha256` (the documented `string | Buffer` argument), matching the
landed surface in `tests/cases/stdlib/01/crypto/sha256/`.

## Why cpp and rust are omitted

Neither the C++ standard library nor Rust `std` includes SHA-256. A single
dependency-free entry would have to vendor a hash implementation, which would
compare third-party dependencies rather than the language's standard-library
surface. The suite therefore skips those two rows
(`TASK_OMIT_LANGUAGES` in `../../runner/run_suite.py` prints the exact reason) and
still gates cp3 against go/js/python.

## Run one by hand

From each language's folder:

```bash
# go
go build -o sha256-churn.exe main.go
./sha256-churn.exe

# js
node main.mjs

# python
python main.py

# cp3
build/bin/cp3c.exe build benchmarks/cases/sha256-churn/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/sha256-churn/cp3/src/index.cp3
```

The oracle value is re-derived independently by `../../runner/oracle.py`
(`sha256_churn_checksum`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the tier with `python benchmarks/runner/run_benchmark.py --only sha256-churn`.
