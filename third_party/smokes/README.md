# third_party/smokes — Ledger F smoke programs

Design + build instructions: `docs/design/vendor-smoke.md`.

| source | library | link |
| --- | --- | --- |
| `xxhash_smoke.cpp` | XXHash (`../xxhash`) | `-L ../xxhash/lib/windows-x64 -lxxhash` |
| `simdutf_smoke.cpp` | simdutf (`../simdutf`) | `-L ../simdutf/lib/windows-x64 -lsimdutf` |
| `unordered_dense_smoke.cpp` | ankerl unordered_dense (`../unordered_dense`) | header-only |
| `fast_float_smoke.cpp` | fast_float (`../fast_float`) | header-only |
| `concurrentqueue_smoke.cpp` | moodycamel ConcurrentQueue (`../moodycamel`) | header-only, `-pthread` |

Run the full 3-variant matrix (dynamic / `-static-libgcc -static-libstdc++` /
`-static`, evidence to `build/scratch/vend/smoke/SMOKE-RUNS.txt`):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File third_party/smokes/run_smokes.ps1
```

Not yet wired into `third_party/build.ps1` — see the design doc, §Wiring.
