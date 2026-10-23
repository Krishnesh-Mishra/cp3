# Benchmark results

`README.md` documents the suite; this file stores the recorded numbers. Every
block below is verbatim runner output. A complete default-scale full-suite run
rewrites the generated latest-run block automatically; a single-task run
(`--only ...`) never touches this file.

## Latest full run

<!-- latest-full-run:start -->
_No full-suite run recorded yet._
<!-- latest-full-run:end -->

## Historical labeled runs

The blocks below are kept as recorded. They predate the tiered gate and the
CP3002 blocker, so their gate lines use the older single 1.2x format.

### Labeled run (2026-10-18, post-P1)

Machine: Intel Core i7-12700H (12th Gen, 6P+8E), 20 logical CPUs, Windows 11
(10.0.26200). Toolchains: g++ 16.1.0 (MinGW-W64 UCRT), rustc 1.95.0, go
1.26.4, node v24.15.0, python 3.14.5, cp3c 0.1.0-beta.1 (the canonical binary
rebuilt after the P1 codegen wave, SHA-256
`C051967809915DAE897618FC9F5CC0D5C4516329E991DC3C190DE9CC37257432`, recorded
in `meta.txt`; verified unchanged across both runs below). Command: `python
benchmarks/run_benchmark.py --reps 2 --hello-reps 15 --results
benchmarks/bench/results/2026-10-18T0803-local` from the repo root, with
`bench/.build/` cleared immediately before, so every native build below is a
fresh compile. The raw file is committed verbatim at
`bench/results/2026-10-18T0803-local/run.txt` (with `meta.txt` beside it); the
`[run ] ...` progress lines are removed here for readability, everything else
is verbatim output.

```
benchmarks bench suite -- 2026-10-18T08:03:13
machine: Intel64 Family 6 Model 154 Stepping 3, GenuineIntel | 20 logical CPUs | Windows-11-10.0.26200-SP0
runner:  python 3.14.5 (C:\Program Files\Python314\python.exe)
  g++     g++ (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r2) 16.1.0
  rustc   rustc 1.95.0 (59807616e 2026-04-14)
  go      go version go1.26.4 windows/amd64
  node    v24.15.0
  python  Python 3.14.5
  cp3c    cp3c 0.1.0-beta.1
every number below is a wall-clock measurement from this run; load phases use persistent keep-alive connections (one per client)

== health-server ==
lang    variant                         build_ms  boot_ms   p50_ms   p95_ms     req/s  cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
----------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp     hand-rolled sockets (Winsock2)      1347    545.8     0.05     0.07   11524.6        -         -         5.4        60  warmup 100, 300 latency reqs, 8 conns x 3s
rust    std::net, no crates                  353    524.3     0.06     0.10   12827.6        -         -         4.6       174  warmup 100, 300 latency reqs, 8 conns x 3s
go      net/http                            1205    509.0     0.10     0.14    8424.0        -         -        55.7      6205  warmup 100, 300 latency reqs, 8 conns x 3s
js      express (installed)                    -    521.6     0.12     0.23   10746.4        -         -        63.7         -  warmup 100, 300 latency reqs, 8 conns x 3s
python  http.server (threaded)                 -    538.7     0.10     0.14   11977.5        -         -        23.4         -  warmup 100, 300 latency reqs, 8 conns x 3s
cp3     -                                     13        -        -        -         -        -         -           -         -  not runnable yet: main.cp3:24:13: error: CP3002: codegen: CallExpr ('call to function 'createServer' not declared in this unit') is not implemented in M1

== compute-single == expected = 131434424 (total Collatz steps for 1..1,000,000)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           411       151 163/151              131434424                  ref            -         -         4.0        19  
rust          148       130 208/130              131434424                  ok             -         -         3.6       122  
go            486       193 312/193              131434424                  ok             -         -         5.7      1750  
js              -      1081 1110/1081            131434424                  ok             -         -        38.3         -  
python          -      9622 9622/9642            131434424                  ok             -         -        11.5         -  
cp3          4944       162 180/162              131434424                  ok         1.08x     1.25x         4.1       108  

== compute-single-int == expected = 131434424 (total Collatz steps for 1..1,000,000)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           400       145 173/145              131434424                  ref            -         -         4.0        19  
rust          141       131 213/131              131434424                  ok             -         -         3.7       123  
go            494       196 311/196              131434424                  ok             -         -         5.5      1750  
js              -      1099 1122/1099            131434424                  ok             -         -        38.2         -  
python          -      9490 9490/9710            131434424                  ok             -         -        11.4         -  
cp3            13         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:25:10: error: CP3002: codegen: Identifier ('returned value outside M1') is not implemented in M1

== compute-multi == expected = 131434424 (same total split across all cores)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           651        23 58/23                131434424                  ref            -         -         4.8        21  
rust          274        19 67/19                131434424                  ok             -         -         4.2       154  
go            535        30 152/30               131434424                  ok             -         -         6.8      1759  
js              -       261 261/262              131434424                  ok             -         -       282.2         -  
python          -      1343 1343/1400            131434424                  ok             -         -        21.0         -  
cp3            14         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:34:15: error: CP3002: codegen: CallExpr ('parallel.*') is not implemented in M1

== hello-world == expected = Hello, world! | spawn -> exit wall time, best and median of 15 runs
lang     build_ms   best_ms median_ms all_runs_ms          output           match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
--------------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           414       6.7       8.8 35/10/7/7/7/7/7/8/9/8/10/10/9/9/10 Hello, world!    ref            -         -         3.0        19  
rust          139       6.0       6.2 41/6/6/6/6/6/6/6/6/7/6/6/6/8/7 Hello, world!    ok             -         -         2.6       122  
go            509       5.8       6.3 126/6/6/6/6/8/6/6/7/6/6/7/12/8/6 Hello, world!    ok             -         -         5.7      1750  
js              -      37.8      42.4 48/42/45/44/46/41/43/39/41/38/42/38/43/40/43 Hello, world!    ok             -         -        35.1         -  
python          -      22.4      23.8 30/24/24/24/23/24/28/23/23/26/23/22/24/23/27 Hello, world!    ok             -         -        11.5         -  
cp3          5336       5.9       7.7 44/7/6/6/7/7/9/8/8/8/9/8/7/8/8 Hello, world!    ok         0.89x     0.99x         3.1       106  

== string-churn == expected = 852167 | scale: default (50000 iterations)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           549      1445 1591/1445            852167                     ref            -         -         4.4        23  
rust          244       214 434/214              852167                     ok             -         -         3.9       165  
go            504      2216 2378/2216            852167                     ok             -         -        80.7      1764  
js              -      1910 1954/1910            852167                     ok             -         -        46.3         -  
python          -      1229 1253/1229            852167                     ok             -         -        11.6         -  
cp3          5292      1596 1596/1603            852167                     ok         1.10x     7.46x         4.5       115  

== word-frequency == expected = 37425343
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           588        45 74/45                37425343                   ref            -         -         4.1        23  
rust          248        49 72/49                37425343                   ok             -         -         3.8       130  
go            485        33 152/33               37425343                   ok             -         -         5.6      1762  
js              -       169 169/177              37425343                   ok             -         -        37.0         -  
python          -       509 512/509              37425343                   ok             -         -        11.5         -  
cp3            14         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:27:1: error: CP3302: cannot find type name 'Map'

== object-churn == expected = 90641269
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           562       119 135/119              90641269                   ref            -         -        61.8        20  
rust          189       115 341/115              90641269                   ok             -         -        61.4       124  
go            515        52 163/52               90641269                   ok             -         -        45.8      1751  
js              -       314 341/314              90641269                   ok             -         -       303.6         -  
python          -      1405 1405/1427            90641269                   ok             -         -       235.0         -  
cp3            11         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:17:1: error: CP3002: not implemented yet: class declarations

== array-growth == expected = 200000010000000
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           471        93 120/93               200000010000000            ref            -         -       215.6        20  
rust          169        95 132/95               200000010000000            ok             -         -       260.6       124  
go            504       183 276/183              200000010000000            ok             -         -       455.7      1750  
js              -       278 278/286              200000010000000            ok             -         -       363.1         -  
python          -      1458 1458/1485            200000010000000            ok             -         -       830.3         -  
cp3            12         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:15:7: error: CP3002: codegen: VariableDeclarator ('declared type outside M1') is not implemented in M1

== closure-capture == expected = 16666688333331 998521500000
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           628        73 105/73               16666688333331 998521500000 ref            -         -       182.1        21  
rust          195        13 114/13               16666688333331 998521500000 ok             -         -         2.7       125  
go            499        16 129/16               16666688333331 998521500000 ok             -         -         4.8      1752  
js              -       630 630/644              16666688333331 998521500000 ok             -         -       293.0         -  
python          -      1175 1208/1175            16666688333331 998521500000 ok             -         -        11.8         -  
cp3            16         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:20:7: error: CP3002: codegen: VariableDeclarator ('declared type outside M1') is not implemented in M1

== interface-dispatch == expected = 37999986 66000000
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           619       117 145/117              37999986 66000000          ref            -         -       111.4        22  
rust          223       102 334/102              37999986 66000000          ok             -         -        90.6       126  
go            484        39 151/39               37999986 66000000          ok             -         -        59.9      1754  
js              -       429 444/429              37999986 66000000          ok             -         -       388.3         -  
python          -      1277 1301/1277            37999986 66000000          ok             -         -       237.5         -  
cp3            15         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:18:1: error: CP3207: syntax error: expected ';'

== generics-mono == expected = 998986007 998986007
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp           432        17 38/17                998986007 998986007        ref            -         -         4.3        19  
rust          154        20 93/20                998986007 998986007        ok             -         -         3.9       124  
go            476        19 139/19               998986007 998986007        ok             -         -         5.6      1750  
js              -        62 67/62                998986007 998986007        ok             -         -        40.0         -  
python          -       500 528/500              998986007 998986007        ok             -         -        11.5         -  
cp3            13         - -                    -                          -              -         -           -         -  not runnable yet: main.cp3:14:1: error: CP3207: syntax error: expected ';'

== summary: cp3 vs native ==
task                   cp3/cpp   cp3/rust
------------------------------------------
health-server                -          -
compute-single           1.08x      1.25x
compute-single-int           -          -
compute-multi                -          -
hello-world              0.89x      0.99x
string-churn             1.10x      7.46x
word-frequency               -          -
object-churn                 -          -
array-growth                 -          -
closure-capture              -          -
interface-dispatch           -          -
generics-mono                -          -
------------------------------------------
geomean over runnable rows     1.02x (n=3)     2.10x (n=3)
ratios are cp3 time divided by the reference time; lower is better

== performance gate == D64 target: cp3 within about 5% of cpp/rust; BIG WARN over 1.20x cpp/rust or slower than go/js/python at all; WARN over 1.10x cpp/rust
BIG WARN (fix this): compute-single: cp3 162 ms is 1.25x rust (130 ms), tripwire 1.20x
WARN string-churn: cp3 1596 ms is 1.10x cpp (1445 ms), watch 1.10x
BIG WARN (fix this): string-churn: cp3 1596 ms is 7.46x rust (214 ms), tripwire 1.20x
BIG WARN (fix this): string-churn: cp3 1596 ms is 1.30x python (1229 ms), tripwire 1.00x
note: hello-world: cp3 5.9 ms vs go 5.8 ms is noise-scale, ratio not gated
gate: 3 BIG WARN(s), 1 WARN(s); rerun with --strict to fail on warnings
```

### Stress preset (2026-10-18, post-P1)

The `stress` scale of `string-churn` exists to stretch the fast runtimes: at
300,000 iterations the fast pack runs for roughly 10-21 seconds, so GC cycles
and allocator behavior have time to show up instead of hiding in startup
noise. Command: `python benchmarks/run_benchmark.py --only string-churn
--churn-scale stress --reps 2 --results
benchmarks/bench/results/2026-10-18T0808-local-stress`. The five non-cp3
entries take the scale as a runtime argument and reused their builds from the
labeled run above (`build: cached` in the table); the cp3 entry re-bakes the
scale into its source and was compiled fresh (4839 ms). Raw output committed
at `bench/results/2026-10-18T0808-local-stress/run.txt`.

```
benchmarks bench suite -- 2026-10-18T08:08:32
machine: Intel64 Family 6 Model 154 Stepping 3, GenuineIntel | 20 logical CPUs | Windows-11-10.0.26200-SP0
runner:  python 3.14.5 (C:\Program Files\Python314\python.exe)
  g++     g++ (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r2) 16.1.0
  rustc   rustc 1.95.0 (59807616e 2026-04-14)
  go      go version go1.26.4 windows/amd64
  node    v24.15.0
  python  Python 3.14.5
  cp3c    cp3c 0.1.0-beta.1
every number below is a wall-clock measurement from this run; load phases use persistent keep-alive connections (one per client)

== string-churn == expected = 254546 | scale: stress (300000 iterations)
lang     build_ms   best_ms all_runs_ms          output                     match    cp3/cpp  cp3/rust peak_rss_mb binary_kb  note
---------------------------------------------------------------------------------------------------------------------------------------------------------------------
cpp             -     13277 13277/13451          254546                     ref            -         -         4.4        23  build: cached
rust            -      1135 1135/1156            254546                     ok             -         -         3.9       165  build: cached
go              -     20956 21081/20956          254546                     ok             -         -        84.7      1764  build: cached
js              -     20105 20105/20615          254546                     ok             -         -        71.1         -  
python          -     10147 10147/10340          254546                     ok             -         -        11.5         -  
cp3          4839     13435 13435/13439          254546                     ok         1.01x    11.83x         4.5       115  

== summary: cp3 vs native ==
task                   cp3/cpp   cp3/rust
------------------------------------------
string-churn             1.01x     11.83x
------------------------------------------
geomean over runnable rows     1.01x (n=1)    11.83x (n=1)
ratios are cp3 time divided by the reference time; lower is better

== performance gate == D64 target: cp3 within about 5% of cpp/rust; BIG WARN over 1.20x cpp/rust or slower than go/js/python at all; WARN over 1.10x cpp/rust
BIG WARN (fix this): string-churn: cp3 13435 ms is 11.83x rust (1135 ms), tripwire 1.20x
BIG WARN (fix this): string-churn: cp3 13435 ms is 1.32x python (10147 ms), tripwire 1.00x
gate: 2 BIG WARN(s), 0 WARN(s); rerun with --strict to fail on warnings
```

Rust stays well under two seconds at this scale because its string handling
is simply that fast; that is the result, not a measurement problem.
