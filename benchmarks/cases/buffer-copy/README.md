# buffer-copy - rotate, slice and rebuild a 64 KiB byte-pattern buffer 5,000 times

Allocation- and copy-heavy binary-data workload, identical in all six
languages. Build a 65,536-byte buffer by repeating the 64-byte seed

```
0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!?
```

then reshape it 5,000 times. On iteration `k`:

1. `offset = (k * 4093 + 17) % 65536`
2. `marker = seed[k % 64]`
3. rotate the buffer left by `offset` bytes and overwrite the new first byte
   with `marker` (a fresh 64 KiB buffer every iteration)
4. fold the first 96 bytes: for each of `'a'` and `'7'`, take the first
   occurrence (`+ 1`, so "absent" folds as `0`), then
   `checksum = (checksum * 131 + 65536 + first_a + first_7) % 1000003`

One line, one integer: `688024`.

Each iteration allocates and copies a full 64 KiB buffer, so the task
exercises buffer allocation, slice/copy, and reassembly rather than raw byte
arithmetic. The offsets and marker cycle are fixed constants, so the output is
deterministic and integer-exact in every language.

## Language entries

| language | buffer type | reassembly |
| --- | --- | --- |
| cp3 | `std/buffer` `Buffer` | `Buffer.from(marker + head.toString() + tail.toString())`, where `head`/`tail` are owning `buf.slice(...)` copies |
| cpp | `std::vector<std::uint8_t>` | reserve + `push_back(marker)` + two range `insert`s |
| rust | `Vec<u8>` | `Vec::with_capacity` + `push` + two `extend_from_slice` |
| go | `[]byte` | `make([]byte, 0, size)` + three `append`s |
| js | Node `Buffer` | `Buffer.concat([Buffer.from([marker]), buf.subarray(offset + 1), buf.subarray(0, offset)])` |
| python | `bytearray` | `bytearray((marker,)) + buf[offset + 1:] + buf[:offset]` |

## Fairness notes

- **cp3's Buffer is immutable.** The landed `std/buffer` surface is
  `Buffer.from`, `Buffer.alloc`, `buf.toString`, `buf.slice` and `buf.length`
  (value semantics; `slice` copies). There is no byte-write API, so the
  one-byte overwrite is expressed while reassembling the new buffer:
  `marker + head.toString() + tail.toString()` through `Buffer.from`. The
  other entries overwrite `next[0]` after their reassembly; the resulting
  bytes are identical.
- **Node's `Buffer.slice`/`subarray` are views, not copies.** The JS entry uses
  `Buffer.concat`, which allocates and copies the new 64 KiB buffer exactly
  like the other entries' reassembly. Using `slice` there would skip the copy
  this task is about; the inline fairness note in `js/main.mjs` says so.
- **No negative modulo trap.** `indexOf`/`find`/`IndexByte` return `-1` for an
  absent byte; every entry folds `position + 1`, so the fold is never negative
  and Python's non-negative `%` agrees with the other languages.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O2 -o buffer-copy.exe main.cpp
./buffer-copy.exe

# rust
rustc -O main.rs -o buffer-copy.exe
./buffer-copy.exe

# go
go build -o buffer-copy.exe main.go
./buffer-copy.exe

# js
node main.mjs

# python
python main.py

# cp3
build/bin/cp3c.exe build benchmarks/cases/buffer-copy/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/buffer-copy/cp3/src/index.cp3
```

The oracle value is re-derived independently by `../../runner/oracle.py`
(`buffer_copy_checksum`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the tier with `python benchmarks/runner/run_benchmark.py --only buffer-copy`.
