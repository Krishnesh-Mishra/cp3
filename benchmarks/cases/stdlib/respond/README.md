# respond - JSON render + FNV

10000 records, names with one-in-eight quote/backslash escapes, manual JSON render. Emit text length and FNV-1a hash.

Run: `python benchmarks/runner/run_benchmark.py --only respond`

## CP3 status

`cp3/src/index.cp3` is the natural std-only implementation. It uses the
current native/string surface, with no hand-written `if` chains:

| Surface | Status |
| --- | --- |
| Exact 64-bit integer literals | Available with `typeSystem: "rust"` (set in `cp3/package.json`); `1469598103934665603` keeps its exact value |
| `String.fromCharCode(code)` | Available; builds each name byte (`'a' + r % 26`, plus `"`/`\`) |
| Wrapping `u64` multiplication | Checked `*` throws on overflow by design; FNV-1a uses the explicit `u64.wrappingMul(x)` escape hatch |

Already present: `String#charCodeAt` (a byte `number`), string indexing
`s[i]` (also a byte `number`), `parseTo<r_'u64'>` of those bytes, and
`parseTo(value, string)`, which renders a native `u64` exactly and is used
here instead of the number-only `u64.toString()`.
