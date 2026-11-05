# respond - JSON render + FNV

10000 records, names with one-in-eight quote/backslash escapes, manual JSON render. Emit text length and FNV-1a hash.

Run: `python benchmarks/runner/run_benchmark.py --only respond`

## CP3 status: not runnable yet

`cp3/src/index.cp3` is the natural std-only implementation; it deliberately
does not fall back to hand-written `if` chains. The surface it needs:

| Missing surface | Why the benchmark needs it |
| --- | --- |
| `String.fromCharCode(code)` | Build the name byte (`'a' + r % 26`, plus `"`/`\`) |
| Exact 64-bit integer literals | `1469598103934665603` is rounded as a `number` literal |
| Wrapping `u64` multiplication | FNV-1a's `h * 1099511628211` wraps modulo 2^64; cp3 native arithmetic traps on overflow |

Already present: `String#charCodeAt` (a byte `number`), string indexing
`s[i]` (also a byte `number`), `parseTo<r_'u64'>` of those bytes, and
`parseTo(value, string)`, which renders a native `u64` exactly and is used
here instead of the number-only `u64.toString()`.
