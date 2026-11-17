

## Latest results by test

<!-- latest-by-test:start -->
<!-- test:health-server:start -->
### health-server
_Updated 2026-11-17T10:07:33 by `python benchmarks/runner/run_benchmark.py`._

```text
  * health-server  median boot/req/s; pooled latency p50/p95; CV = run-to-run variation %
  lang        ok/run  boot ms  p50 ms  p95 ms   req/s  RPS CV%  errors  vs cp3  RSS MiB  bin KiB
  ----------------------------------------------------------------------------------------------
  java           1/1   1148.7    0.35    0.53   9,753        -       0       -    167.5        4
  dotnet         1/1     48.2    0.77    1.58   9,620        -       0       -     50.5        8
  rust           1/1    512.3    0.19    0.38   9,523        -       0       -      9.1     2154
  cpp            1/1     57.3    0.19    0.42   9,519        -       0       -      6.4       60
  go             1/1    539.5    0.28    0.51   8,694        -       0       -     70.3    15630
  js-fastify     1/1   1562.9    0.20    0.32   6,358        -       0       -     68.2        -
  js             1/1   2087.7    0.26    0.44   4,444        -       0       -     64.4        -
  python         1/1   2124.8    0.70    1.07     515        -       0       -      4.1        -
  cp3            0/1        -       -       -  no run        -       0       -        -        -
  goose          0/1        -       -       -  no run        -       0       -        -        -
    ! cp3  not runnable yet: C:\Users\Krishnesh\dev\cp3\src/std/collections/include\cp3/std/collections.hpp:67:10: fatal error:
           'concurrentqueue.h' file not found
```
<!-- test:health-server:end -->

<!-- test:health-server-multi:start -->
### health-server-multi
_Updated 2026-11-17T10:09:21 by `python benchmarks/runner/run_benchmark.py`._

```text
  * health-server-multi  median boot/req/s; pooled latency p50/p95; CV = run-to-run variation %
  lang        ok/run  boot ms  p50 ms  p95 ms   req/s  RPS CV%  errors  vs cp3  RSS MiB  bin KiB
  ----------------------------------------------------------------------------------------------
  java           1/1    595.3    0.29    0.60  81,809        -       0       -    367.4        4
  cpp            1/1    509.9    0.17    0.33  69,130        -      12       -      6.4       61
  rust           1/1    514.8    0.12    0.27  65,645        -       0       -     10.7     2154
  go             1/1    528.0    0.13    0.27  45,249        -       0       -     73.3    15630
  js             1/1   2578.4    0.20    0.37  36,360        -       0       -     50.5        -
  dotnet         1/1    558.1    0.22    0.32  33,843        -       0       -    361.7        9
  js-fastify     1/1   2077.9    0.15    0.60  29,835        -       0       -     54.5        -
  python         1/1   2629.9    0.59    0.85   9,280        -       0       -      4.1        -
  cp3            0/1        -       -       -  no run        -       0       -        -        -
  goose          0/1        -       -       -  no run        -       0       -        -        -
    ! cp3  not runnable yet: C:\Users\Krishnesh\dev\cp3\src/std/collections/include\cp3/std/collections.hpp:67:10: fatal error:
           'concurrentqueue.h' file not found
```
<!-- test:health-server-multi:end -->

<!-- test:compute-single:start -->
### compute-single
_Updated 2026-11-17T10:15:14 by `python benchmarks/runner/run_benchmark.py`._

```text
  * compute-single  expected = 131434424 (total Collatz steps for 1..1,000,000) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms   p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        187.8    186.0    199.6  19.5   2.41x faster      4.2      122
  cpp         206.4    204.8    216.8  11.7   2.19x faster      4.7       19
  go          282.7    274.0    295.8  22.2   1.60x faster      6.0     1750
  java        363.4    357.9    371.3   1.4   1.25x faster     39.1        1
  cp3         452.7    447.2    527.7   9.8       baseline      5.4      414
  js         1536.5   1526.3   1552.4   0.7   3.39x slower     38.9        -
  python    18666.8  15640.7  27672.1  22.6  41.24x slower     11.0        -
    +  output verified: 7/7 match oracle '131434424'

gate: BIG WARN (fix this): compute-single: cp3 453 ms is 2.19x cpp (206 ms), tripwire 1.20x
gate: BIG WARN (fix this): compute-single: cp3 453 ms is 2.41x rust (188 ms), tripwire 1.20x
gate: BIG WARN (fix this): compute-single: cp3 453 ms is 1.60x go (283 ms), tripwire 1.00x
gate: BIG WARN (fix this): compute-single: cp3 453 ms is 1.25x java (363 ms), tripwire 1.00x
```
<!-- test:compute-single:end -->

<!-- test:compute-single-int:start -->
### compute-single-int
_Updated 2026-11-17T10:22:40 by `python benchmarks/runner/run_benchmark.py`._

```text
  * compute-single-int  expected = 131434424 (total Collatz steps for 1..1,000,000) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms   p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        188.9    184.7    195.2  22.8   2.41x faster      4.2      123
  cpp         206.7    204.9    207.9   8.1   2.20x faster      4.7       19
  go          278.7    275.9    290.8  18.1   1.64x faster      6.0     1750
  java        373.8    363.4    380.1   1.8   1.22x faster     38.6        1
  cp3         455.8    450.2    556.7  12.9       baseline      5.4      414
  js         1534.2   1523.7   1545.7   0.5   3.37x slower     38.9        -
  python    27453.2  15297.5  28105.9  16.7  60.24x slower     11.0        -
    +  output verified: 7/7 match oracle '131434424'

gate: BIG WARN (fix this): compute-single-int: cp3 456 ms is 2.20x cpp (207 ms), tripwire 1.20x
gate: BIG WARN (fix this): compute-single-int: cp3 456 ms is 2.41x rust (189 ms), tripwire 1.20x
gate: BIG WARN (fix this): compute-single-int: cp3 456 ms is 1.64x go (279 ms), tripwire 1.00x
gate: BIG WARN (fix this): compute-single-int: cp3 456 ms is 1.22x java (374 ms), tripwire 1.00x
```
<!-- test:compute-single-int:end -->

<!-- test:compute-multi:start -->
### compute-multi
_Updated 2026-11-17T22:16:34 by `python benchmarks/runner/run_benchmark.py --only compute-multi --reps 15`._

```text
  ◆ compute-multi  expected = 131434424 (same total split across all cores) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  rust        29.96    27.46   35.64  17.3  15.49x faster      4.8      849
  cpp         41.98    39.46   45.88  10.8  11.05x faster      5.0       24
  go          47.07    43.84   54.55  13.7   9.86x faster      6.9     1759
  java        141.5    124.5   156.6   9.9   3.28x faster     41.7        1
  js          339.4    322.8   350.0   2.6   1.37x faster    283.5        -
  cp3         463.9    452.8   636.9  14.4       baseline      6.1      705
  python     1694.3   1567.0  1911.4   6.8   3.65x slower     19.8        -
    ✓  output verified: 7/7 match oracle '131434424'

gate: BIG WARN (fix this) multi-core compute-multi: cp3 464 ms is 11.05x cpp (42 ms); multi-core rows must be faster than cpp (target < 1.00x)
gate: BIG WARN (fix this): compute-multi: cp3 464 ms is 15.49x rust (30 ms), tripwire 1.20x
gate: BIG WARN (fix this): compute-multi: cp3 464 ms is 9.86x go (47 ms), tripwire 1.00x
gate: BIG WARN (fix this): compute-multi: cp3 464 ms is 1.37x js (339 ms), tripwire 1.00x
gate: BIG WARN (fix this): compute-multi: cp3 464 ms is 3.28x java (142 ms), tripwire 1.00x
```
<!-- test:compute-multi:end -->

<!-- test:hello-world:start -->
### hello-world
_Updated 2026-11-17T10:23:27 by `python benchmarks/runner/run_benchmark.py`._

```text
  * hello-world  expected = Hello, world! | spawn -> exit wall time; 15 runs | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        13.68    10.40   17.01   83.1  1.13x faster      4.3      122
  go          14.27    11.44   19.19  221.4  1.08x faster      4.0     1750
  cpp         14.92    12.13   19.75   75.4  1.04x faster      4.6       19
  cp3         15.45    12.04   84.91  149.7      baseline      5.3      410
  python      36.47    33.09   43.15   17.7  2.36x slower     11.0        -
  js          61.50    55.81   65.01    4.2  3.98x slower     37.6        -
  java        70.21    65.97   73.87    4.3  4.54x slower     39.2        0
    +  output verified: 7/7 match oracle 'Hello, world!'

gate: WARN hello-world: cp3 15 ms is 1.13x rust (14 ms), watch 1.05x
gate: BIG WARN (fix this): hello-world: cp3 15 ms is 1.08x go (14 ms), tripwire 1.00x
```
<!-- test:hello-world:end -->

<!-- test:dev-reload:start -->
### dev-reload
_Updated 2026-11-17T10:25:07 by `python benchmarks/runner/run_benchmark.py`._

```text
  * dev-reload  expected = Hello, world! | one edit -> build -> rerun cycle; median/best/p95 across 15 alternating edits | medi..
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  python      36.69    35.65   48.73   12.6  45.10x faster        -        -
  js          61.43    59.22   65.23    4.2  26.93x faster        -        -
  rust        475.4    423.9   514.5   47.3   3.48x faster        -        -
  java        542.2    526.2   574.2    4.2   3.05x faster        -        -
  cpp         651.8    573.0   728.3   73.3   2.54x faster        -        -
  go         1156.9   1101.3  1257.4  228.5   1.43x faster        -        -
  cp3        1654.6   1300.4  1989.7   43.4       baseline        -        -
    +  output verified: 7/7 match oracle 'Hello, world!'

gate: BIG WARN (fix this): dev-reload: cp3 1655 ms is 2.54x cpp (652 ms), tripwire 1.20x
gate: BIG WARN (fix this): dev-reload: cp3 1655 ms is 3.48x rust (475 ms), tripwire 1.20x
gate: BIG WARN (fix this): dev-reload: cp3 1655 ms is 1.43x go (1157 ms), tripwire 1.00x
gate: BIG WARN (fix this): dev-reload: cp3 1655 ms is 26.93x js (61 ms), tripwire 1.00x
gate: BIG WARN (fix this): dev-reload: cp3 1655 ms is 45.10x python (37 ms), tripwire 1.00x
gate: BIG WARN (fix this): dev-reload: cp3 1655 ms is 3.05x java (542 ms), tripwire 1.00x
```
<!-- test:dev-reload:end -->

<!-- test:giant-repo-dev-reload:start -->
### giant-repo-dev-reload
_Updated 2026-11-17T10:27:43 by `python benchmarks/runner/run_benchmark.py`._

```text
  * giant-repo-dev-reload  expected = Hello, world! | one edit -> build -> rerun cycle; median/best/p95 across 15 alternating e..
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  python      341.0    278.4   430.8   15.2  6.66x faster        -        -
  js          368.2    343.7   405.0    5.8  6.17x faster        -        -
  rust        663.7    556.6   824.9  108.9  3.42x faster        -        -
  cpp         746.6    684.5   842.5  108.8  3.04x faster        -        -
  go         1310.8   1187.7  1392.8   49.0  1.73x faster        -        -
  java       1623.8   1558.2  1676.7    2.6  1.40x faster        -        -
  cp3        2271.9   2002.5  2723.9  174.3      baseline        -        -
    +  output verified: 7/7 match oracle 'Hello, world!'

gate: BIG WARN (fix this): giant-repo-dev-reload: cp3 2272 ms is 3.04x cpp (747 ms), tripwire 1.20x
gate: BIG WARN (fix this): giant-repo-dev-reload: cp3 2272 ms is 3.42x rust (664 ms), tripwire 1.20x
gate: BIG WARN (fix this): giant-repo-dev-reload: cp3 2272 ms is 1.73x go (1311 ms), tripwire 1.00x
gate: BIG WARN (fix this): giant-repo-dev-reload: cp3 2272 ms is 6.17x js (368 ms), tripwire 1.00x
gate: BIG WARN (fix this): giant-repo-dev-reload: cp3 2272 ms is 6.66x python (341 ms), tripwire 1.00x
gate: BIG WARN (fix this): giant-repo-dev-reload: cp3 2272 ms is 1.40x java (1624 ms), tripwire 1.00x
```
<!-- test:giant-repo-dev-reload:end -->

<!-- test:string-churn:start -->
### string-churn
_Updated 2026-11-17T10:31:30 by `python benchmarks/runner/run_benchmark.py`._

```text
  * string-churn  expected = 852167 | scale: default (50000 iterations) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cp3         320.7    316.6   405.3  15.0       baseline      6.0      430
  rust        349.4    342.8   354.7  25.2   1.09x slower      4.6      166
  python     1992.9   1967.3  2031.8   1.1   6.21x slower     11.1        -
  cpp        2282.8   2217.8  2343.2   4.2   7.12x slower      5.1       23
  js         2535.8   2507.5  2581.4   1.0   7.91x slower     46.7        -
  java       2655.6   2575.8  2687.9   1.6   8.28x slower    553.4        2
  go         4500.7   4089.3  5547.5  10.9  14.03x slower     96.6     1765
    +  output verified: 7/7 match oracle '852167'
```
<!-- test:string-churn:end -->

<!-- test:sha256-churn:start -->
### sha256-churn
_Updated 2026-11-17T10:31:39 by `python benchmarks/runner/run_benchmark.py`._

```text
  * sha256-churn  expected = 218229 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  go          63.23    58.28   116.7  68.8       -      6.3     1922
  python      132.5    130.2   148.8   4.8       -     13.3        -
  js          143.2    137.8   150.2   4.8       -     42.1        -
  java        165.8    159.9   172.8   2.6       -     49.9        1
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:30:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 4/4 match oracle '218229'
```
<!-- test:sha256-churn:end -->

<!-- test:buffer-copy:start -->
### buffer-copy
_Updated 2026-11-17T10:31:54 by `python benchmarks/runner/run_benchmark.py`._

```text
  * buffer-copy  expected = 688024 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        19.79    17.88   24.96  185.7  1.50x faster      4.6      125
  cp3         29.70    26.53   110.2  115.7      baseline      5.6      428
  go          63.58    54.80   74.68   64.2  2.14x slower     52.7     1752
  python      66.20    62.86   90.82   15.0  2.23x slower     11.8        -
  cpp         98.98    20.17   102.9   76.2  3.33x slower      4.8       21
  java        153.5    150.1   159.1    2.2  5.17x slower    264.6        1
  js          252.0    242.5   263.7    4.2  8.49x slower    115.5        -
    +  output verified: 7/7 match oracle '688024'

gate: BIG WARN (fix this): buffer-copy: cp3 30 ms is 1.50x rust (20 ms), tripwire 1.20x
```
<!-- test:buffer-copy:end -->

<!-- test:typed-array-sum:start -->
### typed-array-sum
_Updated 2026-11-17T10:32:27 by `python benchmarks/runner/run_benchmark.py`._

```text
  * typed-array-sum  expected = 135618465 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  cp3         25.82    20.33   109.0  122.6       baseline     14.0      438
  cpp         27.39    26.54   32.00   48.0   1.06x slower     13.3       22
  rust        36.32    33.44   42.96  167.7   1.41x slower     12.8      128
  go          52.83    36.95   93.58  105.4   2.05x slower     15.7     1750
  java        110.4    105.6   116.6    3.4   4.27x slower     48.6        1
  js          131.6    94.46   192.5   27.1   5.10x slower     49.5        -
  python     1449.5   1440.6  1464.6    0.8  56.13x slower     20.2        -
    +  output verified: 7/7 match oracle '135618465'
```
<!-- test:typed-array-sum:end -->

<!-- test:math-kernel:start -->
### math-kernel
_Updated 2026-11-17T10:33:27 by `python benchmarks/runner/run_benchmark.py`._

```text
  * math-kernel  expected = 177047762 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        36.67    34.98   40.89  152.1   1.84x faster      4.6      124
  cp3         67.37    63.98   193.5   69.4       baseline      5.4      418
  cpp         107.9    106.9   110.7   25.8   1.60x slower      4.7       20
  go          118.5    108.9   188.5   43.6   1.76x slower      6.2     1762
  java        153.9    148.8   159.1    2.5   2.28x slower     39.2        1
  js          155.0    139.8   250.4   26.4   2.30x slower     40.5        -
  python     2915.4   2892.8  3068.2    2.1  43.27x slower     11.2        -
    +  output verified: 7/7 match oracle '177047762'

gate: BIG WARN (fix this): math-kernel: cp3 67 ms is 1.84x rust (37 ms), tripwire 1.20x
```
<!-- test:math-kernel:end -->

<!-- test:word-frequency:start -->
### word-frequency
_Updated 2026-11-17T10:33:50 by `python benchmarks/runner/run_benchmark.py`._

```text
  * word-frequency  expected = 37425343 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  go          52.60    46.31   71.14  110.2       -      6.0     1762
  cpp         60.99    59.37   63.84   24.7       -      4.8       23
  rust        66.88    64.55   70.56   42.7       -      4.4      130
  java        141.3    134.2   154.0    6.8       -     73.9        1
  js          244.4    225.7   328.2   13.8       -     39.1        -
  python      655.6    641.0   672.0    8.4       -     11.0        -
  cp3             -        -       -      -       -        -        -
    ! cp3  not runnable yet: C:\Users\Krishnesh\dev\cp3\src/std/collections/include\cp3/std/collections.hpp:67:10: fatal error:
           'concurrentqueue.h' file not found
    +  output verified: 6/6 match oracle '37425343'
```
<!-- test:word-frequency:end -->

<!-- test:object-churn:start -->
### object-churn
_Updated 2026-11-17T10:34:34 by `python benchmarks/runner/run_benchmark.py`._

```text
  * object-churn  expected = 90641269 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  go          90.05    66.37   131.7  55.2       -     46.2     1751
  cpp         142.8    139.6   150.7  11.2       -     62.5       20
  rust        145.5    142.2   152.0  71.2       -     62.0      124
  java        160.8    148.1   167.5   4.9       -    131.1        1
  js          425.7    408.9   444.9   5.4       -    304.1        -
  python     1787.8   1700.3  1829.9   2.8       -    234.7        -
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:40:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '90641269'
```
<!-- test:object-churn:end -->

<!-- test:array-growth:start -->
### array-growth
_Updated 2026-11-17T10:35:22 by `python benchmarks/runner/run_benchmark.py`._

```text
  * array-growth  expected = 200000010000000 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cp3         81.36    73.29   160.7  40.9       baseline    158.0      416
  cpp         114.1    111.1   122.9  15.9   1.40x slower    261.2       20
  rust        125.2    118.6   133.5  19.7   1.54x slower    261.2      124
  go          226.6    202.9   272.7  35.9   2.78x slower    625.1     1750
  java        234.2    228.8   241.6   2.0   2.88x slower    554.9        1
  js          348.6    340.4   352.9   2.0   4.28x slower    353.7        -
  python     1753.7   1734.3  1775.9   0.8  21.55x slower    831.7        -
    +  output verified: 7/7 match oracle '200000010000000'
```
<!-- test:array-growth:end -->

<!-- test:closure-capture:start -->
### closure-capture
_Updated 2026-11-17T10:36:02 by `python benchmarks/runner/run_benchmark.py`._

```text
  * closure-capture  expected = 16666688333331 998521500000 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  rust        23.71    20.82   34.58  153.2       -      4.2      125
  go          32.83    21.61   60.39  147.3       -      6.2     1752
  cpp         87.24    83.12   92.13   16.9       -    182.4       21
  java        155.0    139.4   166.1    6.0       -     78.7        2
  js          780.4    763.6   792.5    2.9       -    293.6        -
  python     1441.9   1433.1  1453.0    0.7       -     11.4        -
  cp3             -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:44:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '16666688333331 998521500000'
```
<!-- test:closure-capture:end -->

<!-- test:interface-dispatch:start -->
### interface-dispatch
_Updated 2026-11-17T10:36:45 by `python benchmarks/runner/run_benchmark.py`._

```text
  * interface-dispatch  expected = 37999986 66000000 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  go          53.63    49.82   58.65  76.0       -     63.8     1754
  rust        127.3    124.7   132.9  70.2       -     90.7      126
  cpp         145.1    142.8   150.7  10.7       -    111.6       22
  java        194.7    177.0   208.8   5.6       -    154.8        2
  js          563.2    524.9   583.7   3.4       -    388.8        -
  python     1642.6   1519.8  1682.3   2.6       -    236.9        -
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:77:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '37999986 66000000'
```
<!-- test:interface-dispatch:end -->

<!-- test:generics-mono:start -->
### generics-mono
_Updated 2026-11-17T10:37:09 by `python benchmarks/runner/run_benchmark.py`._

```text
  * generics-mono  expected = 998986007 998986007 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  cpp         25.67    23.68   31.03   83.4  14.43x faster      4.7       19
  rust        32.18    28.08   39.21   81.7  11.51x faster      4.2      124
  go          34.32    28.37   75.10  121.7  10.79x faster      6.0     1750
  js          91.16    81.28   143.1   26.8   4.06x faster     40.7        -
  java        126.2    101.2   137.5   10.4   2.94x faster     51.7        2
  cp3         370.4    366.6   413.4   11.2       baseline      5.4      420
  python      621.2    615.7   628.6    0.9   1.68x slower     11.0        -
    +  output verified: 7/7 match oracle '998986007 998986007'

gate: BIG WARN (fix this): generics-mono: cp3 370 ms is 14.43x cpp (26 ms), tripwire 1.20x
gate: BIG WARN (fix this): generics-mono: cp3 370 ms is 11.51x rust (32 ms), tripwire 1.20x
gate: BIG WARN (fix this): generics-mono: cp3 370 ms is 10.79x go (34 ms), tripwire 1.00x
gate: BIG WARN (fix this): generics-mono: cp3 370 ms is 4.06x js (91 ms), tripwire 1.00x
gate: BIG WARN (fix this): generics-mono: cp3 370 ms is 2.94x java (126 ms), tripwire 1.00x
```
<!-- test:generics-mono:end -->

<!-- test:json-roundtrip:start -->
### json-roundtrip
_Updated 2026-11-17T21:07:58 by `python benchmarks/runner/run_benchmark.py --only json-parse-large,json-roundtrip,compute-multi --reps 5 --cp3c build\windows-release\bin\cp3c.exe`._

```text
  ◆ json-roundtrip  expected = 664346 | 20,000 parse+stringify round trips of one 310-byte record | median/best/p95 ms; CV = var…
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  js          106.0    101.8   115.2   4.7  1.55x faster     39.7        -
  go          142.1    141.2   584.3  85.8  1.16x faster     12.6     2154
  cp3         164.5    162.2   564.1  63.1      baseline      5.8      768
  java        221.3    208.5   238.7   4.8  1.34x slower    151.4        4
  python      276.7    267.9   296.6   4.8  1.68x slower     12.7        -
    ✓  output verified: 5/5 match oracle '664346'

gate: BIG WARN (fix this): json-roundtrip: cp3 165 ms is 1.16x go (142 ms), tripwire 1.00x
gate: BIG WARN (fix this): json-roundtrip: cp3 165 ms is 1.55x js (106 ms), tripwire 1.00x
```
<!-- test:json-roundtrip:end -->

<!-- test:async-fanout:start -->
### async-fanout
_Updated 2026-11-17T10:38:00 by `python benchmarks/runner/run_benchmark.py`._

```text
  * async-fanout  expected = 99898 | 10,000-timer fan-out joined at once, then 10,000 chained awaits | median/best/p95 ms; CV =..
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  go          22.13    18.45   26.49  147.6   1.01x faster     11.5     1760
  cp3         22.28    19.28   104.3  140.5       baseline      9.1      814
  js          83.28    73.53   91.96    7.2   3.74x slower     51.6        -
  java        226.3    198.9   240.3    6.6  10.16x slower     99.4        3
  python      245.9    240.1   270.4    4.0  11.04x slower     35.0        -
    +  output verified: 5/5 match oracle '99898'

gate: BIG WARN (fix this): async-fanout: cp3 22 ms is 1.01x go (22 ms), tripwire 1.00x
```
<!-- test:async-fanout:end -->

<!-- test:env-injection:start -->
### env-injection
_Updated 2026-11-17T10:38:23 by `python benchmarks/runner/run_benchmark.py`._

```text
  * env-injection  expected = 133644 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  java        127.6    120.2   148.4   7.8       -     62.7        1
  rust        138.7    133.2   254.6  29.8       -      4.2      126
  cpp         162.4    159.0   168.8  31.7       -      4.7       21
  go          180.8    162.5   251.4  36.0       -     13.2     1755
  js          330.7    324.6   544.4  65.6       -     40.7        -
  python      378.9    375.9   391.3   2.1       -     11.1        -
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:33:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '133644'
```
<!-- test:env-injection:end -->

<!-- test:file-roundtrip:start -->
### file-roundtrip
_Updated 2026-11-17T10:38:53 by `python benchmarks/runner/run_benchmark.py`._

```text
  * file-roundtrip  expected = 698140 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  java        217.7    206.7   231.3   3.5       -     43.8        2
  rust        262.5    239.0   323.7  16.8       -      4.3      148
  cpp         271.8    242.2   504.8  38.9       -      5.3       33
  js          319.0    305.4   337.8   3.4       -     41.0        -
  python      362.3    344.9   373.6   3.4       -     13.9        -
  go          367.2    263.0   561.7  33.9       -      8.4     1900
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:29:1: error: CP3411: type mismatch: cannot mix signed 'u64' with unsigned 'i64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '698140'
```
<!-- test:file-roundtrip:end -->

<!-- test:path-join:start -->
### path-join
_Updated 2026-11-17T10:39:40 by `python benchmarks/runner/run_benchmark.py`._

```text
  * path-join  expected = 425577 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------------
  go          92.46    70.61   102.4  53.4  2.90x faster     11.8     1768
  rust        192.0    189.2   200.2  50.7  1.39x faster      4.2      135
  cp3         267.7    262.6   351.3  15.7      baseline      5.9      426
  java        323.1    308.0   339.3  71.5  1.21x slower    155.2        1
  js          414.5    403.0   433.2   3.1  1.55x slower     42.3        -
  cpp         532.7    526.5   798.5  18.7  1.99x slower      5.0       31
  python      774.7    771.0   806.1   3.1  2.89x slower     11.0        -
    +  output verified: 7/7 match oracle '425577'

gate: BIG WARN (fix this): path-join: cp3 268 ms is 1.39x rust (192 ms), tripwire 1.20x
gate: BIG WARN (fix this): path-join: cp3 268 ms is 2.90x go (92 ms), tripwire 1.00x
```
<!-- test:path-join:end -->

<!-- test:date-format:start -->
### date-format
_Updated 2026-11-17T10:39:49 by `python benchmarks/runner/run_benchmark.py`._

```text
  * date-format  expected = 796897 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         35.41    32.65   37.72   41.8       -      4.7       47
  go          37.20    31.56   80.08  113.8       -      7.5     1815
  js          93.63    89.36   109.5    7.4       -     41.1        -
  java        113.3    105.8   126.7    6.4       -     55.2        2
  python      198.5    195.7   208.2    2.2       -     11.2        -
  cp3             -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:73:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 5/5 match oracle '796897'
```
<!-- test:date-format:end -->

<!-- test:os-probe:start -->
### os-probe
_Updated 2026-11-17T10:40:51 by `python benchmarks/runner/run_benchmark.py`._

```text
  * os-probe  expected = 208981 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------------
  java        201.5    190.6   235.2   9.7  1.21x faster     43.3        2
  cpp         233.5    230.0   239.6   7.7  1.04x faster      4.8       21
  cp3         243.2    237.3   366.7  23.9      baseline      8.5      651
  go          507.8    495.4   611.3  13.6  2.09x slower     11.8     1758
  python      639.6    631.4   659.9   1.8  2.63x slower     14.2        -
  js         1971.9   1947.2  1983.1   0.7  8.11x slower     42.2        -
    +  output verified: 6/6 match oracle '208981'

gate: BIG WARN (fix this): os-probe: cp3 243 ms is 1.21x java (201 ms), tripwire 1.00x
```
<!-- test:os-probe:end -->

<!-- test:binary-trees:start -->
### binary-trees
_Updated 2026-11-17T10:41:11 by `python benchmarks/runner/run_benchmark.py`._

```text
  * binary-trees  expected = 1348958 | full binary trees to depth 13, recursive build and check | median/best/p95 ms; CV = vari..
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        41.11    38.58   69.34  139.8  8.43x faster      4.7      124
  go          65.31    47.20   103.0   93.4  5.30x faster     12.3     1751
  cpp         68.28    65.81   72.78   29.3  5.07x faster      5.8       34
  js          83.65    78.25   112.4   12.7  4.14x faster     46.3        -
  java        87.74    79.41   103.5   12.5  3.95x faster     60.2        1
  python      279.4    273.6   287.9    1.9  1.24x faster     12.6        -
  cp3         346.4    341.3   427.7   18.9      baseline      9.1      420
    +  output verified: 7/7 match oracle '1348958'

gate: BIG WARN (fix this): binary-trees: cp3 346 ms is 5.07x cpp (68 ms), tripwire 1.20x
gate: BIG WARN (fix this): binary-trees: cp3 346 ms is 8.43x rust (41 ms), tripwire 1.20x
gate: BIG WARN (fix this): binary-trees: cp3 346 ms is 5.30x go (65 ms), tripwire 1.00x
gate: BIG WARN (fix this): binary-trees: cp3 346 ms is 4.14x js (84 ms), tripwire 1.00x
gate: BIG WARN (fix this): binary-trees: cp3 346 ms is 1.24x python (279 ms), tripwire 1.00x
gate: BIG WARN (fix this): binary-trees: cp3 346 ms is 3.95x java (88 ms), tripwire 1.00x
```
<!-- test:binary-trees:end -->

<!-- test:map-churn:start -->
### map-churn
_Updated 2026-11-17T10:41:20 by `python benchmarks/runner/run_benchmark.py`._

```text
  * map-churn  expected = 942471769 | 100,000-key hash map, delete/reinsert/update churn | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  rust        18.85    16.27   24.96   69.7       -      6.5      128
  go          20.45    18.87   29.00  146.5       -      8.3     1756
  cpp         22.87    21.46   28.66   88.8       -      9.4       21
  python      70.05    68.37   83.00    8.7       -     24.0        -
  js          82.64    78.60   90.39    5.5       -     47.4        -
  java        127.9    113.9   144.5    9.1       -     71.2        1
  cp3             -        -       -      -       -        -        -
    ! cp3  not runnable yet: C:\Users\Krishnesh\dev\cp3\src/std/collections/include\cp3/std/collections.hpp:67:10: fatal error:
           'concurrentqueue.h' file not found
    +  output verified: 6/6 match oracle '942471769'
```
<!-- test:map-churn:end -->

<!-- test:utf8-text:start -->
### utf8-text
_Updated 2026-11-17T10:42:03 by `python benchmarks/runner/run_benchmark.py`._

```text
  * utf8-text  expected = 78156718 | 417,792-byte UTF-8 text, 32 manual decode rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         47.93    46.24   52.87   33.0       -      5.4       21
  rust        53.22    50.23   56.06   94.0       -      4.6      124
  go          67.62    52.04   86.76  103.4       -      6.5     1756
  java        120.2    116.3   128.5    4.1       -     42.2        2
  js          192.2    185.5   223.3    7.3       -     41.1        -
  python     2180.8   2164.7  2280.9    2.1       -     12.5        -
  cp3             -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:39:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '78156718'
```
<!-- test:utf8-text:end -->

<!-- test:regex-scan:start -->
### regex-scan
_Updated 2026-11-17T10:42:12 by `python benchmarks/runner/run_benchmark.py`._

```text
  * regex-scan  expected = 735703090 | 240,000-byte text, global [0-9]+ scan, 8 rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  js          76.70    70.97   83.73   6.6       -     44.4        -
  python      95.24    90.83   112.0   8.2       -     15.5        -
  go          103.0    93.66   130.3  70.6       -     12.2     2074
  cpp         125.2    123.1   130.8  23.9       -      5.2      130
  java        136.4    117.5   156.0   9.3       -     55.2        1
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:35:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 5/5 match oracle '735703090'
```
<!-- test:regex-scan:end -->

<!-- test:json-parse-large:start -->
### json-parse-large
_Updated 2026-11-17T21:08:12 by `python benchmarks/runner/run_benchmark.py --only json-parse-large,json-roundtrip,compute-multi --reps 5 --cp3c build\windows-release\bin\cp3c.exe`._

```text
  ◆ json-parse-large  expected = 613781 | 204,472-byte JSON record, 20 parse rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  js          87.11    82.64   102.2   8.8  2.41x faster     52.0        -
  go          121.9    118.3   521.4  88.9  1.72x faster     12.7     2154
  python      124.9    122.9   161.1  12.0  1.68x faster     16.6        -
  java        184.3    175.3   199.5   5.0  1.14x faster    131.6        3
  cp3         209.6    193.0   655.1  59.9      baseline     16.1      748
    ✓  output verified: 5/5 match oracle '613781'

gate: BIG WARN (fix this): json-parse-large: cp3 210 ms is 1.72x go (122 ms), tripwire 1.00x
gate: BIG WARN (fix this): json-parse-large: cp3 210 ms is 2.41x js (87 ms), tripwire 1.00x
gate: BIG WARN (fix this): json-parse-large: cp3 210 ms is 1.68x python (125 ms), tripwire 1.00x
gate: BIG WARN (fix this): json-parse-large: cp3 210 ms is 1.14x java (184 ms), tripwire 1.00x
```
<!-- test:json-parse-large:end -->

<!-- test:deep-recursion:start -->
### deep-recursion
_Updated 2026-11-17T10:42:34 by `python benchmarks/runner/run_benchmark.py`._

```text
  * deep-recursion  expected = 292249866 | 5,000-deep recursion, 200 rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cp3         18.01    14.68   158.9  148.2      baseline      5.7      414
  cpp         18.32    13.21   20.66   89.3  1.02x slower      4.7       19
  rust        18.63    13.87   21.56   98.9  1.03x slower      4.2      123
  go          22.72    17.20   27.00  151.8  1.26x slower      6.4     1750
  java        72.82    65.66   79.85   11.3  4.04x slower     39.0        1
  js          76.98    65.60   128.5   27.1  4.27x slower     39.1        -
  python      149.3    145.3   154.5    2.6  8.29x slower     11.8        -
    +  output verified: 7/7 match oracle '292249866'
```
<!-- test:deep-recursion:end -->

<!-- test:exception-cost:start -->
### exception-cost
_Updated 2026-11-17T10:43:39 by `python benchmarks/runner/run_benchmark.py`._

```text
  * exception-cost  expected = 446742259 446742259 | 1,000,000 throw/catch vs sentinel-return iterations | median/best/p95 ms; ..
  lang    median ms  best ms  p95 ms  CV%        vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------------
  java        256.9    241.6   277.0  5.4  4.48x faster    189.3        1
  python      394.5    388.9   406.6  4.4  2.92x faster     11.1        -
  cpp         589.2    584.3   614.5  2.6  1.96x faster      4.9       21
  cp3        1152.2   1145.2  1234.3  4.7      baseline      5.9      426
  js         1615.5   1468.7  1875.9  8.1  1.40x slower     40.7        -
    +  output verified: 5/5 match oracle '446742259 446742259'

gate: BIG WARN (fix this): exception-cost: cp3 1152 ms is 1.96x cpp (589 ms), tripwire 1.20x
gate: BIG WARN (fix this): exception-cost: cp3 1152 ms is 2.92x python (394 ms), tripwire 1.00x
gate: BIG WARN (fix this): exception-cost: cp3 1152 ms is 4.48x java (257 ms), tripwire 1.00x
```
<!-- test:exception-cost:end -->

<!-- test:file-big:start -->
### file-big
_Updated 2026-11-17T10:44:09 by `python benchmarks/runner/run_benchmark.py`._

```text
  * file-big  expected = 779913 | 8 MiB file, 8 write/read rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  js          195.9    189.1   201.5   3.4       -    111.6        -
  cpp         253.4    212.8   271.1   7.7       -     21.2       31
  rust        261.5    231.3   294.8  65.4       -     20.3      146
  go          269.0    226.0   319.3  20.0       -     47.4     1891
  java        297.7    270.1   316.9   5.6       -    129.5        2
  python      493.7    463.4   504.8   3.0       -     37.7        -
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:38:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '779913'
```
<!-- test:file-big:end -->

<!-- test:tree-walk:start -->
### tree-walk
_Updated 2026-11-17T10:44:19 by `python benchmarks/runner/run_benchmark.py`._

```text
  * tree-walk  expected = 16383
66134180 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        16.65    14.08   23.11  146.4  1.41x faster      4.8      841
  go          18.60    15.84   25.49  152.9  1.26x faster      6.2     1752
  cpp         18.90    15.32   22.57   77.2  1.24x faster      4.7       97
  cp3         23.42    22.34   94.70  114.6      baseline      7.3      419
  python      67.02    64.17   82.69   10.4  2.86x slower     12.6        -
  js          80.83    73.09   136.0   26.0  3.45x slower     44.1        -
  java        97.18    83.06   110.7   10.3  4.15x slower     39.6        1
    +  output verified: 7/7 match oracle '16383\n66134180'

gate: BIG WARN (fix this): tree-walk: cp3 23 ms is 1.24x cpp (19 ms), tripwire 1.20x
gate: BIG WARN (fix this): tree-walk: cp3 23 ms is 1.41x rust (17 ms), tripwire 1.20x
gate: BIG WARN (fix this): tree-walk: cp3 23 ms is 1.26x go (19 ms), tripwire 1.00x
```
<!-- test:tree-walk:end -->

<!-- test:graph-bfs:start -->
### graph-bfs
_Updated 2026-11-17T10:44:41 by `python benchmarks/runner/run_benchmark.py`._

```text
  * graph-bfs  cross-check only (no oracle yet) | graph-bfs | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  go          31.77    26.50   45.28  124.4  2.24x faster     13.1     1751
  rust        35.43    33.71   46.88   83.4  2.01x faster     10.9      841
  cpp         40.36    39.14   45.82   38.5  1.77x faster      9.8       47
  cp3         71.24    66.37   217.3   59.0      baseline     16.8      426
  java        153.2    144.8   169.7    6.6  2.15x slower     73.3        1
  js          248.1    221.7   269.0    7.3  3.48x slower     85.9        -
  python      608.9    567.1   639.6    4.7  8.55x slower     32.8        -
    +  output verified: 7/7 match across languages

gate: BIG WARN (fix this): graph-bfs: cp3 71 ms is 1.77x cpp (40 ms), tripwire 1.20x
gate: BIG WARN (fix this): graph-bfs: cp3 71 ms is 2.01x rust (35 ms), tripwire 1.20x
gate: BIG WARN (fix this): graph-bfs: cp3 71 ms is 2.24x go (32 ms), tripwire 1.00x
```
<!-- test:graph-bfs:end -->

<!-- test:strlist:start -->
### strlist
_Updated 2026-11-17T10:45:07 by `python benchmarks/runner/run_benchmark.py`._

```text
  * strlist  expected = 100000
92177148 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cpp         26.25    20.21   40.56   45.2  3.10x faster      9.2       47
  rust        27.83    24.31   39.17   86.3  2.92x faster      9.7      841
  go          35.34    26.06   79.61  101.3  2.30x faster     14.8     1751
  cp3         81.28    74.32   164.0   49.7      baseline     10.5      427
  java        127.0    108.4   138.9    7.5  1.56x slower     52.6        1
  js          278.6    264.4   306.0    5.4  3.43x slower     84.1        -
  python      804.1    772.1   840.6    2.9  9.89x slower     18.9        -
    +  output verified: 7/7 match oracle '100000\n92177148'

gate: BIG WARN (fix this): strlist: cp3 81 ms is 3.10x cpp (26 ms), tripwire 1.20x
gate: BIG WARN (fix this): strlist: cp3 81 ms is 2.92x rust (28 ms), tripwire 1.20x
gate: BIG WARN (fix this): strlist: cp3 81 ms is 2.30x go (35 ms), tripwire 1.00x
```
<!-- test:strlist:end -->

<!-- test:interp:start -->
### interp
_Updated 2026-11-17T10:45:14 by `python benchmarks/runner/run_benchmark.py`._

```text
  * interp  expected = 24
914237 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cpp         14.31    12.78   23.27  113.1  1.00x faster      4.2       48
  cp3         14.35    12.77   97.61  171.4      baseline      5.9      422
  python      35.49    32.97   39.32   17.4  2.47x slower     11.1        -
  java        70.32    66.81   91.95   13.1  4.90x slower     39.0        1
  js              -        -       -      -             -        -        -
    x js  run failed: file:///C:/Users/Krishnesh/dev/cp3/benchmarks/cases/dsa-object/interp/js/main.mjs:3
    +  output verified: 4/4 match oracle '24\n914237'
```
<!-- test:interp:end -->

<!-- test:blur:start -->
### blur
_Updated 2026-11-17T10:45:32 by `python benchmarks/runner/run_benchmark.py`._

```text
  * blur  expected = 256
7734030 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  cpp         17.04    13.14   27.94   66.3   1.11x faster      4.8       49
  rust        17.22    13.83   22.08  157.9   1.09x faster      4.5      843
  cp3         18.83    17.61   120.2  139.3       baseline      7.1      419
  go          22.79    15.65   74.78  153.6   1.21x slower      5.9     1750
  js          81.74    77.25   114.5   16.3   4.34x slower     41.1        -
  java        109.5    106.4   126.1    7.3   5.82x slower     38.9        1
  python      501.3    489.9   699.7   16.8  26.62x slower     12.0        -
    +  output verified: 7/7 match oracle '256\n7734030'

gate: WARN blur: cp3 19 ms is 1.11x cpp (17 ms), watch 1.05x
gate: WARN blur: cp3 19 ms is 1.09x rust (17 ms), watch 1.05x
```
<!-- test:blur:end -->

<!-- test:calc:start -->
### calc
_Updated 2026-11-17T10:45:43 by `python benchmarks/runner/run_benchmark.py`._

```text
  * calc  expected = 3750
1250
580458 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cp3         14.03    11.79   103.0  182.0      baseline      5.9      432
  rust        16.45    12.88   20.18  149.8  1.17x slower      4.7      843
  goose       20.06    10.95   22.95   84.6  1.43x slower      4.2       58
  go          21.64    16.30   24.05  152.0  1.54x slower      5.8     1754
  cpp         24.07    15.94   28.38   72.8  1.71x slower      4.7       54
  python      80.34    76.74   95.79    7.9  5.72x slower     11.4        -
  js          84.32    73.22   117.4   18.4  6.01x slower     43.9        -
  java        94.91    91.05   113.5   10.1  6.76x slower     41.7        2
    +  output verified: 8/8 match oracle '3750\n1250\n580458'
```
<!-- test:calc:end -->

<!-- test:particles:start -->
### particles
_Updated 2026-11-17T10:45:59 by `python benchmarks/runner/run_benchmark.py`._

```text
  * particles  expected = 5000
28315536 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        15.75    14.01   22.14  149.8   1.02x faster      4.8      840
  cp3         16.11    14.40   92.70  144.2       baseline      5.3      426
  goose       17.79    13.39   22.67   74.5   1.10x slower      4.2       58
  go          18.16    12.96   27.55  202.9   1.13x slower      5.9     1751
  cpp         20.01    15.61   22.94   71.8   1.24x slower      4.7       45
  js          77.73    71.12   86.84    7.7   4.82x slower     41.0        -
  java        104.5    92.13   109.9    8.8   6.49x slower     38.0        1
  python      451.1    441.1   461.7    1.5  28.00x slower     12.0        -
    +  output verified: 8/8 match oracle '5000\n28315536'
```
<!-- test:particles:end -->

<!-- test:push:start -->
### push
_Updated 2026-11-17T10:46:20 by `python benchmarks/runner/run_benchmark.py`._

```text
  * push  expected = 1000000
15625
499758894813
750050 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         24.09    19.33   30.53   83.6       -     12.8       46
  rust        25.43    20.32   30.89   87.4       -     21.2      840
  go          28.98    23.45   41.10  111.7       -     22.9     1752
  java        111.0    100.9   128.4    9.1       -     82.0        1
  js          385.5    349.4   400.9    4.7       -    300.6        -
  python      712.0    695.7   755.9    4.0       -    114.1        -
  cp3             -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:13:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '1000000\n15625\n499758894813\n750050'
```
<!-- test:push:end -->

<!-- test:records:start -->
### records
_Updated 2026-11-17T10:46:34 by `python benchmarks/runner/run_benchmark.py`._

```text
  * records  expected = 100000
17463411840 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cpp         17.73    16.07   23.39  142.2  2.92x faster      9.3       47
  goose       19.77    16.61   27.74  105.9  2.62x faster      7.8       58
  rust        21.75    16.50   26.12  145.5  2.38x faster     12.2      841
  go          23.72    17.37   32.72  160.0  2.18x faster     11.4     1752
  cp3         51.82    49.27   110.5   94.6      baseline     16.4      426
  java        86.40    82.55   98.22    7.8  1.67x slower     45.1        2
  js          120.9    115.5   128.2    4.8  2.33x slower     70.6        -
  python      228.7    221.9   236.3    3.0  4.41x slower     26.9        -
    +  output verified: 8/8 match oracle '100000\n17463411840'

gate: BIG WARN (fix this): records: cp3 52 ms is 2.92x cpp (18 ms), tripwire 1.20x
gate: BIG WARN (fix this): records: cp3 52 ms is 2.38x rust (22 ms), tripwire 1.20x
gate: BIG WARN (fix this): records: cp3 52 ms is 2.18x go (24 ms), tripwire 1.00x
```
<!-- test:records:end -->

<!-- test:scene:start -->
### scene
_Updated 2026-11-17T10:46:43 by `python benchmarks/runner/run_benchmark.py`._

```text
  * scene  expected = 23
4747 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        15.75    14.22   19.97  120.0  1.23x faster      4.7      842
  goose       16.35    14.44   21.21   70.1  1.18x faster      3.6       58
  cp3         19.36    13.12   89.34  169.8      baseline      5.3      428
  go          20.32    14.68   29.64  156.1  1.05x slower      5.9     1752
  cpp         20.49    14.46   24.53   75.7  1.06x slower      4.7       50
  python      36.40    33.38   44.19   16.6  1.88x slower     11.1        -
  js          66.82    61.81   80.87   10.2  3.45x slower     37.9        -
  java        74.64    65.88   93.11   16.5  3.85x slower     39.0        2
    +  output verified: 8/8 match oracle '23\n4747'

gate: BIG WARN (fix this): scene: cp3 19 ms is 1.23x rust (16 ms), tripwire 1.20x
```
<!-- test:scene:end -->

<!-- test:sexp:start -->
### sexp
_Updated 2026-11-17T10:46:56 by `python benchmarks/runner/run_benchmark.py`._

```text
  * sexp  cross-check only (no oracle yet) | sexp | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        12.73    10.79   15.61  185.6  16.19x faster      4.7      841
  go          16.78    11.69   22.22  186.3  12.29x faster      6.0     1754
  cpp         22.03    17.55   26.45   95.2   9.36x faster      4.7       56
  java        82.40    67.13   117.7   23.6   2.50x faster     40.1        1
  js          88.84    74.28   109.6   11.8   2.32x faster     44.1        -
  python      102.2    95.87   113.1    6.3   2.02x faster     11.6        -
  cp3         206.1    126.6   320.8   30.4       baseline      6.5      425
    x  rust output '122590\n6764934848165057546'; expected '56938\n13287444230854067034'

gate: BIG WARN (fix this): sexp: cp3 206 ms is 9.36x cpp (22 ms), tripwire 1.20x
gate: BIG WARN (fix this): sexp: cp3 206 ms is 16.19x rust (13 ms), tripwire 1.20x
gate: BIG WARN (fix this): sexp: cp3 206 ms is 12.29x go (17 ms), tripwire 1.00x
gate: BIG WARN (fix this): sexp: cp3 206 ms is 2.32x js (89 ms), tripwire 1.00x
gate: BIG WARN (fix this): sexp: cp3 206 ms is 2.02x python (102 ms), tripwire 1.00x
gate: BIG WARN (fix this): sexp: cp3 206 ms is 2.50x java (82 ms), tripwire 1.00x
```
<!-- test:sexp:end -->

<!-- test:respond:start -->
### respond
_Updated 2026-11-17T10:47:33 by `python benchmarks/runner/run_benchmark.py`._

```text
  * respond  expected = 578079
3348416444655930007 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  goose       15.24    11.52   21.65   81.7   7.55x faster      3.7       58
  go          16.35    14.32   33.66  167.9   7.04x faster     10.1     1756
  rust        16.96    13.52   27.42  110.4   6.79x faster      6.3      842
  cpp         19.37    13.69   28.06   90.0   5.94x faster      6.1       50
  cp3         115.1    86.77   353.6   69.9       baseline      6.5      432
  java        123.7    103.7   141.0   11.3   1.07x slower     43.7        2
  js          137.0    123.7   155.0    8.6   1.19x slower     59.9        -
  python     2032.4    962.3  2133.8   32.4  17.66x slower     13.5        -
    +  output verified: 8/8 match oracle '578079\n3348416444655930007'

gate: BIG WARN (fix this): respond: cp3 115 ms is 5.94x cpp (19 ms), tripwire 1.20x
gate: BIG WARN (fix this): respond: cp3 115 ms is 6.79x rust (17 ms), tripwire 1.20x
gate: BIG WARN (fix this): respond: cp3 115 ms is 7.04x go (16 ms), tripwire 1.00x
```
<!-- test:respond:end -->

<!-- test:sum-array:start -->
### sum-array
_Updated 2026-11-17T10:48:36 by `python benchmarks/runner/run_benchmark.py`._

```text
  * sum-array  expected = 4000000
15987891712 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        31.56    30.15   34.88  108.1   1.75x faster     19.9      841
  cpp         32.30    30.26   40.32   47.5   1.71x faster     19.9       45
  go          48.63    38.11   75.28   73.7   1.14x faster     22.3     1750
  cp3         55.36    53.58   101.2   71.4       baseline     38.2      418
  java        111.9    108.1   117.4    4.0   2.02x slower     54.9        1
  js          620.9    610.2   634.0    1.6  11.22x slower     56.1        -
  python     2961.2   2942.5  2990.7    0.6  53.49x slower    159.3        -
    +  output verified: 7/7 match oracle '4000000\n15987891712'

gate: BIG WARN (fix this): sum-array: cp3 55 ms is 1.71x cpp (32 ms), tripwire 1.20x
gate: BIG WARN (fix this): sum-array: cp3 55 ms is 1.75x rust (32 ms), tripwire 1.20x
gate: BIG WARN (fix this): sum-array: cp3 55 ms is 1.14x go (49 ms), tripwire 1.00x
```
<!-- test:sum-array:end -->
<!-- latest-by-test:end -->

## Latest full run

<!-- latest-full-run:start -->
_Generated by `python benchmarks/runner/run_suite.py` on 2026-11-17T10:48:36._

```text
BENCHMARK SUITE  2026-11-17T10:05:33 | 15 runs/test
  machine  Intel64 Family 6 Model 154 Stepping 3, Genu.. | 20 logical CPUs
  os      Windows-11-10.0.26200-SP0 | Python 3.14.5
  +g++ g++ (MinGW-W64 x86_64-ucrt.. | +rustc rustc 1.95.0 (59807616e 20.. | +cargo cargo 1.95.0 (f2d3ce0bd 20..
  +go go version go1.26.4 window.. | +node v24.15.0 | +python Python 3.14.5
  +java openjdk version "21.0.11" .. | +javac javac 21.0.11 | +cmake cmake version 3.29.2
  +cp3c cp3c 0.2.4 | +dotnet 8.0.427 | +goose usage: goose <command> fil..
  method  median headline; best/p95/CV retained; HTTP uses keep-alive
  > health-server: cpp x1 ...
  > health-server: rust x1 ...
  > health-server: go x1 ...
  > health-server: js x1 ...
  > health-server: js-fastify x1 ...
  > health-server: python x1 ...
  > health-server: java x1 ...
  > health-server: dotnet x1 ...
  > health-server: cp3 x1 ...
  > health-server: goose x1 ...

  * health-server  median boot/req/s; pooled latency p50/p95; CV = run-to-run variation %
  lang        ok/run  boot ms  p50 ms  p95 ms   req/s  RPS CV%  errors  vs cp3  RSS MiB  bin KiB
  ----------------------------------------------------------------------------------------------
  java           1/1   1148.7    0.35    0.53   9,753        -       0       -    167.5        4
  dotnet         1/1     48.2    0.77    1.58   9,620        -       0       -     50.5        8
  rust           1/1    512.3    0.19    0.38   9,523        -       0       -      9.1     2154
  cpp            1/1     57.3    0.19    0.42   9,519        -       0       -      6.4       60
  go             1/1    539.5    0.28    0.51   8,694        -       0       -     70.3    15630
  js-fastify     1/1   1562.9    0.20    0.32   6,358        -       0       -     68.2        -
  js             1/1   2087.7    0.26    0.44   4,444        -       0       -     64.4        -
  python         1/1   2124.8    0.70    1.07     515        -       0       -      4.1        -
  cp3            0/1        -       -       -  no run        -       0       -        -        -
  goose          0/1        -       -       -  no run        -       0       -        -        -
    ! cp3  not runnable yet: C:\Users\Krishnesh\dev\cp3\src/std/collections/include\cp3/std/collections.hpp:67:10: fatal error:
           'concurrentqueue.h' file not found
  > health-server-multi: cpp x1 ...
  > health-server-multi: rust x1 ...
  > health-server-multi: go x1 ...
  > health-server-multi: js x1 ...
  > health-server-multi: js-fastify x1 ...
  > health-server-multi: python x1 ...
  > health-server-multi: java x1 ...
  > health-server-multi: dotnet x1 ...
  > health-server-multi: cp3 x1 ...
  > health-server-multi: goose x1 ...

  * health-server-multi  median boot/req/s; pooled latency p50/p95; CV = run-to-run variation %
  lang        ok/run  boot ms  p50 ms  p95 ms   req/s  RPS CV%  errors  vs cp3  RSS MiB  bin KiB
  ----------------------------------------------------------------------------------------------
  java           1/1    595.3    0.29    0.60  81,809        -       0       -    367.4        4
  cpp            1/1    509.9    0.17    0.33  69,130        -      12       -      6.4       61
  rust           1/1    514.8    0.12    0.27  65,645        -       0       -     10.7     2154
  go             1/1    528.0    0.13    0.27  45,249        -       0       -     73.3    15630
  js             1/1   2578.4    0.20    0.37  36,360        -       0       -     50.5        -
  dotnet         1/1    558.1    0.22    0.32  33,843        -       0       -    361.7        9
  js-fastify     1/1   2077.9    0.15    0.60  29,835        -       0       -     54.5        -
  python         1/1   2629.9    0.59    0.85   9,280        -       0       -      4.1        -
  cp3            0/1        -       -       -  no run        -       0       -        -        -
  goose          0/1        -       -       -  no run        -       0       -        -        -
    ! cp3  not runnable yet: C:\Users\Krishnesh\dev\cp3\src/std/collections/include\cp3/std/collections.hpp:67:10: fatal error:
           'concurrentqueue.h' file not found
  > compute-single: cpp x15 ...
  > compute-single: rust x15 ...
  > compute-single: go x15 ...
  > compute-single: js x15 ...
  > compute-single: python x15 ...
  > compute-single: java x15 ...
  > compute-single: cp3 x15 ...
  - omit  compute-single: goose: goose entry not authored for this task yet

  * compute-single  expected = 131434424 (total Collatz steps for 1..1,000,000) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms   p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        187.8    186.0    199.6  19.5   2.41x faster      4.2      122
  cpp         206.4    204.8    216.8  11.7   2.19x faster      4.7       19
  go          282.7    274.0    295.8  22.2   1.60x faster      6.0     1750
  java        363.4    357.9    371.3   1.4   1.25x faster     39.1        1
  cp3         452.7    447.2    527.7   9.8       baseline      5.4      414
  js         1536.5   1526.3   1552.4   0.7   3.39x slower     38.9        -
  python    18666.8  15640.7  27672.1  22.6  41.24x slower     11.0        -
    +  output verified: 7/7 match oracle '131434424'
  > compute-single-int: cpp x15 ...
  > compute-single-int: rust x15 ...
  > compute-single-int: go x15 ...
  > compute-single-int: js x15 ...
  > compute-single-int: python x15 ...
  > compute-single-int: java x15 ...
  > compute-single-int: cp3 x15 ...
  - omit  compute-single-int: goose: goose entry not authored for this task yet

  * compute-single-int  expected = 131434424 (total Collatz steps for 1..1,000,000) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms   p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        188.9    184.7    195.2  22.8   2.41x faster      4.2      123
  cpp         206.7    204.9    207.9   8.1   2.20x faster      4.7       19
  go          278.7    275.9    290.8  18.1   1.64x faster      6.0     1750
  java        373.8    363.4    380.1   1.8   1.22x faster     38.6        1
  cp3         455.8    450.2    556.7  12.9       baseline      5.4      414
  js         1534.2   1523.7   1545.7   0.5   3.37x slower     38.9        -
  python    27453.2  15297.5  28105.9  16.7  60.24x slower     11.0        -
    +  output verified: 7/7 match oracle '131434424'
  > compute-multi: cpp x15 ...
  > compute-multi: rust x15 ...
  > compute-multi: go x15 ...
  > compute-multi: js x15 ...
  > compute-multi: python x15 ...
  > compute-multi: java x15 ...
  > compute-multi: cp3 x15 ...
  - omit  compute-multi: goose: goose entry not authored for this task yet

  * compute-multi  expected = 131434424 (same total split across all cores) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  rust        27.98    23.23   41.51  87.2       -      4.3      154
  cpp         41.94    32.61   53.86  53.7       -      5.0       21
  go          47.51    42.80   59.70  94.7       -      6.9     1759
  java        132.8    123.9   154.7   8.6       -     40.3        1
  js          312.0    299.3   328.4  10.2       -    286.2        -
  python     1645.5   1547.3  1853.7  17.2       -     19.8        -
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: C:\Users\Krishnesh\dev\cp3\src/runtime/src\parallel\parallel.cpp:44:10: fatal error:
           'tbb/blocked_range.h' file not found
    +  output verified: 6/6 match oracle '131434424'
  > hello-world: cpp x15 ...
  > hello-world: rust x15 ...
  > hello-world: go x15 ...
  > hello-world: js x15 ...
  > hello-world: python x15 ...
  > hello-world: java x15 ...
  > hello-world: cp3 x15 ...
  - omit  hello-world: goose: goose entry not authored for this task yet

  * hello-world  expected = Hello, world! | spawn -> exit wall time; 15 runs | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        13.68    10.40   17.01   83.1  1.13x faster      4.3      122
  go          14.27    11.44   19.19  221.4  1.08x faster      4.0     1750
  cpp         14.92    12.13   19.75   75.4  1.04x faster      4.6       19
  cp3         15.45    12.04   84.91  149.7      baseline      5.3      410
  python      36.47    33.09   43.15   17.7  2.36x slower     11.0        -
  js          61.50    55.81   65.01    4.2  3.98x slower     37.6        -
  java        70.21    65.97   73.87    4.3  4.54x slower     39.2        0
    +  output verified: 7/7 match oracle 'Hello, world!'
  > dev-reload: cpp x15 ...
  > dev-reload: rust x15 ...
  > dev-reload: go x15 ...
  > dev-reload: js x15 ...
  > dev-reload: python x15 ...
  > dev-reload: java x15 ...
  > dev-reload: cp3 x15 ...
  - omit  dev-reload: goose: goose entry not authored for this task yet

  * dev-reload  expected = Hello, world! | one edit -> build -> rerun cycle; median/best/p95 across 15 alternating edits | medi..
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  python      36.69    35.65   48.73   12.6  45.10x faster        -        -
  js          61.43    59.22   65.23    4.2  26.93x faster        -        -
  rust        475.4    423.9   514.5   47.3   3.48x faster        -        -
  java        542.2    526.2   574.2    4.2   3.05x faster        -        -
  cpp         651.8    573.0   728.3   73.3   2.54x faster        -        -
  go         1156.9   1101.3  1257.4  228.5   1.43x faster        -        -
  cp3        1654.6   1300.4  1989.7   43.4       baseline        -        -
    +  output verified: 7/7 match oracle 'Hello, world!'
  > giant-repo-dev-reload: cpp x15 ...
  > giant-repo-dev-reload: rust x15 ...
  > giant-repo-dev-reload: go x15 ...
  > giant-repo-dev-reload: js x15 ...
  > giant-repo-dev-reload: python x15 ...
  > giant-repo-dev-reload: java x15 ...
  > giant-repo-dev-reload: cp3 x15 ...
  - omit  giant-repo-dev-reload: goose: goose entry not authored for this task yet

  * giant-repo-dev-reload  expected = Hello, world! | one edit -> build -> rerun cycle; median/best/p95 across 15 alternating e..
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  python      341.0    278.4   430.8   15.2  6.66x faster        -        -
  js          368.2    343.7   405.0    5.8  6.17x faster        -        -
  rust        663.7    556.6   824.9  108.9  3.42x faster        -        -
  cpp         746.6    684.5   842.5  108.8  3.04x faster        -        -
  go         1310.8   1187.7  1392.8   49.0  1.73x faster        -        -
  java       1623.8   1558.2  1676.7    2.6  1.40x faster        -        -
  cp3        2271.9   2002.5  2723.9  174.3      baseline        -        -
    +  output verified: 7/7 match oracle 'Hello, world!'
  > string-churn: cpp x15 ...
  > string-churn: rust x15 ...
  > string-churn: go x15 ...
  > string-churn: js x15 ...
  > string-churn: python x15 ...
  > string-churn: java x15 ...
  > string-churn: cp3 x15 ...
  - omit  string-churn: goose: goose entry not authored for this task yet

  * string-churn  expected = 852167 | scale: default (50000 iterations) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cp3         320.7    316.6   405.3  15.0       baseline      6.0      430
  rust        349.4    342.8   354.7  25.2   1.09x slower      4.6      166
  python     1992.9   1967.3  2031.8   1.1   6.21x slower     11.1        -
  cpp        2282.8   2217.8  2343.2   4.2   7.12x slower      5.1       23
  js         2535.8   2507.5  2581.4   1.0   7.91x slower     46.7        -
  java       2655.6   2575.8  2687.9   1.6   8.28x slower    553.4        2
  go         4500.7   4089.3  5547.5  10.9  14.03x slower     96.6     1765
    +  output verified: 7/7 match oracle '852167'
  - omit  sha256-churn: cpp: no SHA-256 in the C++ standard library; vendoring a hash implementation compares dependencies
  - omit  sha256-churn: rust: no SHA-256 in Rust std without a crate; adding sha2 compares dependencies
  > sha256-churn: go x15 ...
  > sha256-churn: js x15 ...
  > sha256-churn: python x15 ...
  > sha256-churn: java x15 ...
  > sha256-churn: cp3 x15 ...
  - omit  sha256-churn: goose: goose entry not authored for this task yet

  * sha256-churn  expected = 218229 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  go          63.23    58.28   116.7  68.8       -      6.3     1922
  python      132.5    130.2   148.8   4.8       -     13.3        -
  js          143.2    137.8   150.2   4.8       -     42.1        -
  java        165.8    159.9   172.8   2.6       -     49.9        1
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:30:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 4/4 match oracle '218229'
  > buffer-copy: cpp x15 ...
  > buffer-copy: rust x15 ...
  > buffer-copy: go x15 ...
  > buffer-copy: js x15 ...
  > buffer-copy: python x15 ...
  > buffer-copy: java x15 ...
  > buffer-copy: cp3 x15 ...
  - omit  buffer-copy: goose: goose entry not authored for this task yet

  * buffer-copy  expected = 688024 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        19.79    17.88   24.96  185.7  1.50x faster      4.6      125
  cp3         29.70    26.53   110.2  115.7      baseline      5.6      428
  go          63.58    54.80   74.68   64.2  2.14x slower     52.7     1752
  python      66.20    62.86   90.82   15.0  2.23x slower     11.8        -
  cpp         98.98    20.17   102.9   76.2  3.33x slower      4.8       21
  java        153.5    150.1   159.1    2.2  5.17x slower    264.6        1
  js          252.0    242.5   263.7    4.2  8.49x slower    115.5        -
    +  output verified: 7/7 match oracle '688024'
  > typed-array-sum: cpp x15 ...
  > typed-array-sum: rust x15 ...
  > typed-array-sum: go x15 ...
  > typed-array-sum: js x15 ...
  > typed-array-sum: python x15 ...
  > typed-array-sum: java x15 ...
  > typed-array-sum: cp3 x15 ...
  - omit  typed-array-sum: goose: goose entry not authored for this task yet

  * typed-array-sum  expected = 135618465 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  cp3         25.82    20.33   109.0  122.6       baseline     14.0      438
  cpp         27.39    26.54   32.00   48.0   1.06x slower     13.3       22
  rust        36.32    33.44   42.96  167.7   1.41x slower     12.8      128
  go          52.83    36.95   93.58  105.4   2.05x slower     15.7     1750
  java        110.4    105.6   116.6    3.4   4.27x slower     48.6        1
  js          131.6    94.46   192.5   27.1   5.10x slower     49.5        -
  python     1449.5   1440.6  1464.6    0.8  56.13x slower     20.2        -
    +  output verified: 7/7 match oracle '135618465'
  > math-kernel: cpp x15 ...
  > math-kernel: rust x15 ...
  > math-kernel: go x15 ...
  > math-kernel: js x15 ...
  > math-kernel: python x15 ...
  > math-kernel: java x15 ...
  > math-kernel: cp3 x15 ...
  - omit  math-kernel: goose: goose entry not authored for this task yet

  * math-kernel  expected = 177047762 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        36.67    34.98   40.89  152.1   1.84x faster      4.6      124
  cp3         67.37    63.98   193.5   69.4       baseline      5.4      418
  cpp         107.9    106.9   110.7   25.8   1.60x slower      4.7       20
  go          118.5    108.9   188.5   43.6   1.76x slower      6.2     1762
  java        153.9    148.8   159.1    2.5   2.28x slower     39.2        1
  js          155.0    139.8   250.4   26.4   2.30x slower     40.5        -
  python     2915.4   2892.8  3068.2    2.1  43.27x slower     11.2        -
    +  output verified: 7/7 match oracle '177047762'
  > word-frequency: cpp x15 ...
  > word-frequency: rust x15 ...
  > word-frequency: go x15 ...
  > word-frequency: js x15 ...
  > word-frequency: python x15 ...
  > word-frequency: java x15 ...
  > word-frequency: cp3 x15 ...
  - omit  word-frequency: goose: goose entry not authored for this task yet

  * word-frequency  expected = 37425343 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  go          52.60    46.31   71.14  110.2       -      6.0     1762
  cpp         60.99    59.37   63.84   24.7       -      4.8       23
  rust        66.88    64.55   70.56   42.7       -      4.4      130
  java        141.3    134.2   154.0    6.8       -     73.9        1
  js          244.4    225.7   328.2   13.8       -     39.1        -
  python      655.6    641.0   672.0    8.4       -     11.0        -
  cp3             -        -       -      -       -        -        -
    ! cp3  not runnable yet: C:\Users\Krishnesh\dev\cp3\src/std/collections/include\cp3/std/collections.hpp:67:10: fatal error:
           'concurrentqueue.h' file not found
    +  output verified: 6/6 match oracle '37425343'
  > object-churn: cpp x15 ...
  > object-churn: rust x15 ...
  > object-churn: go x15 ...
  > object-churn: js x15 ...
  > object-churn: python x15 ...
  > object-churn: java x15 ...
  > object-churn: cp3 x15 ...
  - omit  object-churn: goose: goose entry not authored for this task yet

  * object-churn  expected = 90641269 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  go          90.05    66.37   131.7  55.2       -     46.2     1751
  cpp         142.8    139.6   150.7  11.2       -     62.5       20
  rust        145.5    142.2   152.0  71.2       -     62.0      124
  java        160.8    148.1   167.5   4.9       -    131.1        1
  js          425.7    408.9   444.9   5.4       -    304.1        -
  python     1787.8   1700.3  1829.9   2.8       -    234.7        -
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:40:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '90641269'
  > array-growth: cpp x15 ...
  > array-growth: rust x15 ...
  > array-growth: go x15 ...
  > array-growth: js x15 ...
  > array-growth: python x15 ...
  > array-growth: java x15 ...
  > array-growth: cp3 x15 ...
  - omit  array-growth: goose: goose entry not authored for this task yet

  * array-growth  expected = 200000010000000 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cp3         81.36    73.29   160.7  40.9       baseline    158.0      416
  cpp         114.1    111.1   122.9  15.9   1.40x slower    261.2       20
  rust        125.2    118.6   133.5  19.7   1.54x slower    261.2      124
  go          226.6    202.9   272.7  35.9   2.78x slower    625.1     1750
  java        234.2    228.8   241.6   2.0   2.88x slower    554.9        1
  js          348.6    340.4   352.9   2.0   4.28x slower    353.7        -
  python     1753.7   1734.3  1775.9   0.8  21.55x slower    831.7        -
    +  output verified: 7/7 match oracle '200000010000000'
  > closure-capture: cpp x15 ...
  > closure-capture: rust x15 ...
  > closure-capture: go x15 ...
  > closure-capture: js x15 ...
  > closure-capture: python x15 ...
  > closure-capture: java x15 ...
  > closure-capture: cp3 x15 ...
  - omit  closure-capture: goose: goose entry not authored for this task yet

  * closure-capture  expected = 16666688333331 998521500000 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  rust        23.71    20.82   34.58  153.2       -      4.2      125
  go          32.83    21.61   60.39  147.3       -      6.2     1752
  cpp         87.24    83.12   92.13   16.9       -    182.4       21
  java        155.0    139.4   166.1    6.0       -     78.7        2
  js          780.4    763.6   792.5    2.9       -    293.6        -
  python     1441.9   1433.1  1453.0    0.7       -     11.4        -
  cp3             -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:44:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '16666688333331 998521500000'
  > interface-dispatch: cpp x15 ...
  > interface-dispatch: rust x15 ...
  > interface-dispatch: go x15 ...
  > interface-dispatch: js x15 ...
  > interface-dispatch: python x15 ...
  > interface-dispatch: java x15 ...
  > interface-dispatch: cp3 x15 ...
  - omit  interface-dispatch: goose: goose entry not authored for this task yet

  * interface-dispatch  expected = 37999986 66000000 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  go          53.63    49.82   58.65  76.0       -     63.8     1754
  rust        127.3    124.7   132.9  70.2       -     90.7      126
  cpp         145.1    142.8   150.7  10.7       -    111.6       22
  java        194.7    177.0   208.8   5.6       -    154.8        2
  js          563.2    524.9   583.7   3.4       -    388.8        -
  python     1642.6   1519.8  1682.3   2.6       -    236.9        -
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:77:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '37999986 66000000'
  > generics-mono: cpp x15 ...
  > generics-mono: rust x15 ...
  > generics-mono: go x15 ...
  > generics-mono: js x15 ...
  > generics-mono: python x15 ...
  > generics-mono: java x15 ...
  > generics-mono: cp3 x15 ...
  - omit  generics-mono: goose: goose entry not authored for this task yet

  * generics-mono  expected = 998986007 998986007 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  cpp         25.67    23.68   31.03   83.4  14.43x faster      4.7       19
  rust        32.18    28.08   39.21   81.7  11.51x faster      4.2      124
  go          34.32    28.37   75.10  121.7  10.79x faster      6.0     1750
  js          91.16    81.28   143.1   26.8   4.06x faster     40.7        -
  java        126.2    101.2   137.5   10.4   2.94x faster     51.7        2
  cp3         370.4    366.6   413.4   11.2       baseline      5.4      420
  python      621.2    615.7   628.6    0.9   1.68x slower     11.0        -
    +  output verified: 7/7 match oracle '998986007 998986007'
  - omit  json-roundtrip: cpp: no JSON in the C++ standard library; a dependency-free file would vendor a parser
  - omit  json-roundtrip: rust: no JSON in Rust std without a crate; adding serde_json compares dependencies
  > json-roundtrip: go x15 ...
  > json-roundtrip: js x15 ...
  > json-roundtrip: python x15 ...
  > json-roundtrip: java x15 ...
  > json-roundtrip: cp3 x15 ...
  - omit  json-roundtrip: goose: goose entry not authored for this task yet

  * json-roundtrip  expected = 664346 | 20,000 parse+stringify round trips of one 310-byte record | median/best/p95 ms; CV = va..
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  js          112.1    106.6   116.7   4.5       -     40.9        -
  go          155.5    145.4   192.6  44.5       -     13.7     2154
  java        240.9    216.5   264.2   7.2       -    155.5        4
  python      278.6    270.3   286.9   3.2       -     12.6        -
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: C:\Users\Krishnesh\dev\cp3\src/std/json/include\cp3/std/json_write.hpp:57:10: fatal error:
           'glaze/json/write.hpp' file not found
    +  output verified: 4/4 match oracle '664346'
  - omit  async-fanout: cpp: no timers or event loop in the C++ standard library; a file would hand-roll both
  - omit  async-fanout: rust: no timers or executor in Rust std; adding tokio/async-std compares dependencies
  > async-fanout: go x15 ...
  > async-fanout: js x15 ...
  > async-fanout: python x15 ...
  > async-fanout: java x15 ...
  > async-fanout: cp3 x15 ...
  - omit  async-fanout: goose: goose entry not authored for this task yet

  * async-fanout  expected = 99898 | 10,000-timer fan-out joined at once, then 10,000 chained awaits | median/best/p95 ms; CV =..
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  go          22.13    18.45   26.49  147.6   1.01x faster     11.5     1760
  cp3         22.28    19.28   104.3  140.5       baseline      9.1      814
  js          83.28    73.53   91.96    7.2   3.74x slower     51.6        -
  java        226.3    198.9   240.3    6.6  10.16x slower     99.4        3
  python      245.9    240.1   270.4    4.0  11.04x slower     35.0        -
    +  output verified: 5/5 match oracle '99898'
  > env-injection: cpp x15 ...
  > env-injection: rust x15 ...
  > env-injection: go x15 ...
  > env-injection: js x15 ...
  > env-injection: python x15 ...
  > env-injection: java x15 ...
  > env-injection: cp3 x15 ...
  - omit  env-injection: goose: goose entry not authored for this task yet

  * env-injection  expected = 133644 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  java        127.6    120.2   148.4   7.8       -     62.7        1
  rust        138.7    133.2   254.6  29.8       -      4.2      126
  cpp         162.4    159.0   168.8  31.7       -      4.7       21
  go          180.8    162.5   251.4  36.0       -     13.2     1755
  js          330.7    324.6   544.4  65.6       -     40.7        -
  python      378.9    375.9   391.3   2.1       -     11.1        -
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:33:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '133644'
  > file-roundtrip: cpp x15 ...
  > file-roundtrip: rust x15 ...
  > file-roundtrip: go x15 ...
  > file-roundtrip: js x15 ...
  > file-roundtrip: python x15 ...
  > file-roundtrip: java x15 ...
  > file-roundtrip: cp3 x15 ...
  - omit  file-roundtrip: goose: goose entry not authored for this task yet

  * file-roundtrip  expected = 698140 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  java        217.7    206.7   231.3   3.5       -     43.8        2
  rust        262.5    239.0   323.7  16.8       -      4.3      148
  cpp         271.8    242.2   504.8  38.9       -      5.3       33
  js          319.0    305.4   337.8   3.4       -     41.0        -
  python      362.3    344.9   373.6   3.4       -     13.9        -
  go          367.2    263.0   561.7  33.9       -      8.4     1900
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:29:1: error: CP3411: type mismatch: cannot mix signed 'u64' with unsigned 'i64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '698140'
  > path-join: cpp x15 ...
  > path-join: rust x15 ...
  > path-join: go x15 ...
  > path-join: js x15 ...
  > path-join: python x15 ...
  > path-join: java x15 ...
  > path-join: cp3 x15 ...
  - omit  path-join: goose: goose entry not authored for this task yet

  * path-join  expected = 425577 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------------
  go          92.46    70.61   102.4  53.4  2.90x faster     11.8     1768
  rust        192.0    189.2   200.2  50.7  1.39x faster      4.2      135
  cp3         267.7    262.6   351.3  15.7      baseline      5.9      426
  java        323.1    308.0   339.3  71.5  1.21x slower    155.2        1
  js          414.5    403.0   433.2   3.1  1.55x slower     42.3        -
  cpp         532.7    526.5   798.5  18.7  1.99x slower      5.0       31
  python      774.7    771.0   806.1   3.1  2.89x slower     11.0        -
    +  output verified: 7/7 match oracle '425577'
  > date-format: cpp x15 ...
  - omit  date-format: rust: no timezone-aware date formatting in Rust std without a crate; adding chrono compares dependencies
  > date-format: go x15 ...
  > date-format: js x15 ...
  > date-format: python x15 ...
  > date-format: java x15 ...
  > date-format: cp3 x15 ...
  - omit  date-format: goose: goose entry not authored for this task yet

  * date-format  expected = 796897 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         35.41    32.65   37.72   41.8       -      4.7       47
  go          37.20    31.56   80.08  113.8       -      7.5     1815
  js          93.63    89.36   109.5    7.4       -     41.1        -
  java        113.3    105.8   126.7    6.4       -     55.2        2
  python      198.5    195.7   208.2    2.2       -     11.2        -
  cp3             -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:73:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 5/5 match oracle '796897'
  > os-probe: cpp x15 ...
  - omit  os-probe: rust: no hostname/platform probe in Rust std without libc or a crate; hand-rolling it compares bindings
  > os-probe: go x15 ...
  > os-probe: js x15 ...
  > os-probe: python x15 ...
  > os-probe: java x15 ...
  > os-probe: cp3 x15 ...
  - omit  os-probe: goose: goose entry not authored for this task yet

  * os-probe  expected = 208981 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------------
  java        201.5    190.6   235.2   9.7  1.21x faster     43.3        2
  cpp         233.5    230.0   239.6   7.7  1.04x faster      4.8       21
  cp3         243.2    237.3   366.7  23.9      baseline      8.5      651
  go          507.8    495.4   611.3  13.6  2.09x slower     11.8     1758
  python      639.6    631.4   659.9   1.8  2.63x slower     14.2        -
  js         1971.9   1947.2  1983.1   0.7  8.11x slower     42.2        -
    +  output verified: 6/6 match oracle '208981'
  > binary-trees: cpp x15 ...
  > binary-trees: rust x15 ...
  > binary-trees: go x15 ...
  > binary-trees: js x15 ...
  > binary-trees: python x15 ...
  > binary-trees: java x15 ...
  > binary-trees: cp3 x15 ...
  - omit  binary-trees: goose: goose entry not authored for this task yet

  * binary-trees  expected = 1348958 | full binary trees to depth 13, recursive build and check | median/best/p95 ms; CV = vari..
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        41.11    38.58   69.34  139.8  8.43x faster      4.7      124
  go          65.31    47.20   103.0   93.4  5.30x faster     12.3     1751
  cpp         68.28    65.81   72.78   29.3  5.07x faster      5.8       34
  js          83.65    78.25   112.4   12.7  4.14x faster     46.3        -
  java        87.74    79.41   103.5   12.5  3.95x faster     60.2        1
  python      279.4    273.6   287.9    1.9  1.24x faster     12.6        -
  cp3         346.4    341.3   427.7   18.9      baseline      9.1      420
    +  output verified: 7/7 match oracle '1348958'
  > map-churn: cpp x15 ...
  > map-churn: rust x15 ...
  > map-churn: go x15 ...
  > map-churn: js x15 ...
  > map-churn: python x15 ...
  > map-churn: java x15 ...
  > map-churn: cp3 x15 ...
  - omit  map-churn: goose: goose entry not authored for this task yet

  * map-churn  expected = 942471769 | 100,000-key hash map, delete/reinsert/update churn | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  rust        18.85    16.27   24.96   69.7       -      6.5      128
  go          20.45    18.87   29.00  146.5       -      8.3     1756
  cpp         22.87    21.46   28.66   88.8       -      9.4       21
  python      70.05    68.37   83.00    8.7       -     24.0        -
  js          82.64    78.60   90.39    5.5       -     47.4        -
  java        127.9    113.9   144.5    9.1       -     71.2        1
  cp3             -        -       -      -       -        -        -
    ! cp3  not runnable yet: C:\Users\Krishnesh\dev\cp3\src/std/collections/include\cp3/std/collections.hpp:67:10: fatal error:
           'concurrentqueue.h' file not found
    +  output verified: 6/6 match oracle '942471769'
  > utf8-text: cpp x15 ...
  > utf8-text: rust x15 ...
  > utf8-text: go x15 ...
  > utf8-text: js x15 ...
  > utf8-text: python x15 ...
  > utf8-text: java x15 ...
  > utf8-text: cp3 x15 ...
  - omit  utf8-text: goose: goose entry not authored for this task yet

  * utf8-text  expected = 78156718 | 417,792-byte UTF-8 text, 32 manual decode rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         47.93    46.24   52.87   33.0       -      5.4       21
  rust        53.22    50.23   56.06   94.0       -      4.6      124
  go          67.62    52.04   86.76  103.4       -      6.5     1756
  java        120.2    116.3   128.5    4.1       -     42.2        2
  js          192.2    185.5   223.3    7.3       -     41.1        -
  python     2180.8   2164.7  2280.9    2.1       -     12.5        -
  cp3             -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:39:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '78156718'
  > regex-scan: cpp x15 ...
  - omit  regex-scan: rust: no regex engine in Rust std; adding the regex crate compares dependencies
  > regex-scan: go x15 ...
  > regex-scan: js x15 ...
  > regex-scan: python x15 ...
  > regex-scan: java x15 ...
  > regex-scan: cp3 x15 ...
  - omit  regex-scan: goose: goose entry not authored for this task yet

  * regex-scan  expected = 735703090 | 240,000-byte text, global [0-9]+ scan, 8 rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  js          76.70    70.97   83.73   6.6       -     44.4        -
  python      95.24    90.83   112.0   8.2       -     15.5        -
  go          103.0    93.66   130.3  70.6       -     12.2     2074
  cpp         125.2    123.1   130.8  23.9       -      5.2      130
  java        136.4    117.5   156.0   9.3       -     55.2        1
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:35:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 5/5 match oracle '735703090'
  - omit  json-parse-large: cpp: no JSON in the C++ standard library; a dependency-free file would vendor a parser
  - omit  json-parse-large: rust: no JSON in Rust std without a crate; adding serde_json compares dependencies
  > json-parse-large: go x15 ...
  > json-parse-large: js x15 ...
  > json-parse-large: python x15 ...
  > json-parse-large: java x15 ...
  > json-parse-large: cp3 x15 ...
  - omit  json-parse-large: goose: goose entry not authored for this task yet

  * json-parse-large  expected = 613781 | 204,472-byte JSON record, 20 parse rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  js          88.13    84.19   96.08   9.3       -     52.2        -
  python      126.4    124.0   128.9   4.7       -     16.7        -
  go          138.7    121.8   165.3  57.4       -     13.2     2154
  java        178.6    171.3   187.8   4.1       -    132.4        3
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: C:\Users\Krishnesh\dev\cp3\src/std/json/include\cp3/std/json_write.hpp:57:10: fatal error:
           'glaze/json/write.hpp' file not found
    +  output verified: 4/4 match oracle '613781'
  > deep-recursion: cpp x15 ...
  > deep-recursion: rust x15 ...
  > deep-recursion: go x15 ...
  > deep-recursion: js x15 ...
  > deep-recursion: python x15 ...
  > deep-recursion: java x15 ...
  > deep-recursion: cp3 x15 ...
  - omit  deep-recursion: goose: goose entry not authored for this task yet

  * deep-recursion  expected = 292249866 | 5,000-deep recursion, 200 rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cp3         18.01    14.68   158.9  148.2      baseline      5.7      414
  cpp         18.32    13.21   20.66   89.3  1.02x slower      4.7       19
  rust        18.63    13.87   21.56   98.9  1.03x slower      4.2      123
  go          22.72    17.20   27.00  151.8  1.26x slower      6.4     1750
  java        72.82    65.66   79.85   11.3  4.04x slower     39.0        1
  js          76.98    65.60   128.5   27.1  4.27x slower     39.1        -
  python      149.3    145.3   154.5    2.6  8.29x slower     11.8        -
    +  output verified: 7/7 match oracle '292249866'
  > exception-cost: cpp x15 ...
  - omit  exception-cost: rust: no exceptions in Rust; Result is the returned-value path, not throw/catch
  - omit  exception-cost: go: no exceptions in Go; error values are the returned-value path, not throw/catch
  > exception-cost: js x15 ...
  > exception-cost: python x15 ...
  > exception-cost: java x15 ...
  > exception-cost: cp3 x15 ...
  - omit  exception-cost: goose: goose entry not authored for this task yet

  * exception-cost  expected = 446742259 446742259 | 1,000,000 throw/catch vs sentinel-return iterations | median/best/p95 ms; ..
  lang    median ms  best ms  p95 ms  CV%        vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------------
  java        256.9    241.6   277.0  5.4  4.48x faster    189.3        1
  python      394.5    388.9   406.6  4.4  2.92x faster     11.1        -
  cpp         589.2    584.3   614.5  2.6  1.96x faster      4.9       21
  cp3        1152.2   1145.2  1234.3  4.7      baseline      5.9      426
  js         1615.5   1468.7  1875.9  8.1  1.40x slower     40.7        -
    +  output verified: 5/5 match oracle '446742259 446742259'
  > file-big: cpp x15 ...
  > file-big: rust x15 ...
  > file-big: go x15 ...
  > file-big: js x15 ...
  > file-big: python x15 ...
  > file-big: java x15 ...
  > file-big: cp3 x15 ...
  - omit  file-big: goose: goose entry not authored for this task yet

  * file-big  expected = 779913 | 8 MiB file, 8 write/read rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  js          195.9    189.1   201.5   3.4       -    111.6        -
  cpp         253.4    212.8   271.1   7.7       -     21.2       31
  rust        261.5    231.3   294.8  65.4       -     20.3      146
  go          269.0    226.0   319.3  20.0       -     47.4     1891
  java        297.7    270.1   316.9   5.6       -    129.5        2
  python      493.7    463.4   504.8   3.0       -     37.7        -
  cp3             -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:38:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '779913'
  > tree-walk: cpp x15 ...
  > tree-walk: rust x15 ...
  > tree-walk: go x15 ...
  > tree-walk: js x15 ...
  > tree-walk: python x15 ...
  > tree-walk: java x15 ...
  > tree-walk: cp3 x15 ...
  - omit  tree-walk: goose: goose entry not authored for this task yet

  * tree-walk  expected = 16383
66134180 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        16.65    14.08   23.11  146.4  1.41x faster      4.8      841
  go          18.60    15.84   25.49  152.9  1.26x faster      6.2     1752
  cpp         18.90    15.32   22.57   77.2  1.24x faster      4.7       97
  cp3         23.42    22.34   94.70  114.6      baseline      7.3      419
  python      67.02    64.17   82.69   10.4  2.86x slower     12.6        -
  js          80.83    73.09   136.0   26.0  3.45x slower     44.1        -
  java        97.18    83.06   110.7   10.3  4.15x slower     39.6        1
    +  output verified: 7/7 match oracle '16383\n66134180'
  > graph-bfs: cpp x15 ...
  > graph-bfs: rust x15 ...
  > graph-bfs: go x15 ...
  > graph-bfs: js x15 ...
  > graph-bfs: python x15 ...
  > graph-bfs: java x15 ...
  > graph-bfs: cp3 x15 ...
  - omit  graph-bfs: goose: goose entry not authored for this task yet

  * graph-bfs  cross-check only (no oracle yet) | graph-bfs | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  go          31.77    26.50   45.28  124.4  2.24x faster     13.1     1751
  rust        35.43    33.71   46.88   83.4  2.01x faster     10.9      841
  cpp         40.36    39.14   45.82   38.5  1.77x faster      9.8       47
  cp3         71.24    66.37   217.3   59.0      baseline     16.8      426
  java        153.2    144.8   169.7    6.6  2.15x slower     73.3        1
  js          248.1    221.7   269.0    7.3  3.48x slower     85.9        -
  python      608.9    567.1   639.6    4.7  8.55x slower     32.8        -
    +  output verified: 7/7 match across languages
  > strlist: cpp x15 ...
  > strlist: rust x15 ...
  > strlist: go x15 ...
  > strlist: js x15 ...
  > strlist: python x15 ...
  > strlist: java x15 ...
  > strlist: cp3 x15 ...
  - omit  strlist: goose: goose entry not authored for this task yet

  * strlist  expected = 100000
92177148 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cpp         26.25    20.21   40.56   45.2  3.10x faster      9.2       47
  rust        27.83    24.31   39.17   86.3  2.92x faster      9.7      841
  go          35.34    26.06   79.61  101.3  2.30x faster     14.8     1751
  cp3         81.28    74.32   164.0   49.7      baseline     10.5      427
  java        127.0    108.4   138.9    7.5  1.56x slower     52.6        1
  js          278.6    264.4   306.0    5.4  3.43x slower     84.1        -
  python      804.1    772.1   840.6    2.9  9.89x slower     18.9        -
    +  output verified: 7/7 match oracle '100000\n92177148'
  - lru: pending (no cases folder yet) -- skipped
  > interp: cpp x15 ...
  - omit  interp: rust: no virtual dispatch in Rust; enum + match is the idiomatic analogue (kept for cross-language comparison
          but listed here to match exception-cost)
  - omit  interp: go: no inheritance in Go; interface + type-switch is the idiomatic analogue
  > interp: js x15 ...
  > interp: python x15 ...
  > interp: java x15 ...
  > interp: cp3 x15 ...
  - omit  interp: goose: goose entry not authored for this task yet

  * interp  expected = 24
914237 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cpp         14.31    12.78   23.27  113.1  1.00x faster      4.2       48
  cp3         14.35    12.77   97.61  171.4      baseline      5.9      422
  python      35.49    32.97   39.32   17.4  2.47x slower     11.1        -
  java        70.32    66.81   91.95   13.1  4.90x slower     39.0        1
  js              -        -       -      -             -        -        -
    x js  run failed: file:///C:/Users/Krishnesh/dev/cp3/benchmarks/cases/dsa-object/interp/js/main.mjs:3
    +  output verified: 4/4 match oracle '24\n914237'
  > blur: cpp x15 ...
  > blur: rust x15 ...
  > blur: go x15 ...
  > blur: js x15 ...
  > blur: python x15 ...
  > blur: java x15 ...
  > blur: cp3 x15 ...
  - omit  blur: goose: goose entry not authored for this task yet

  * blur  expected = 256
7734030 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  cpp         17.04    13.14   27.94   66.3   1.11x faster      4.8       49
  rust        17.22    13.83   22.08  157.9   1.09x faster      4.5      843
  cp3         18.83    17.61   120.2  139.3       baseline      7.1      419
  go          22.79    15.65   74.78  153.6   1.21x slower      5.9     1750
  js          81.74    77.25   114.5   16.3   4.34x slower     41.1        -
  java        109.5    106.4   126.1    7.3   5.82x slower     38.9        1
  python      501.3    489.9   699.7   16.8  26.62x slower     12.0        -
    +  output verified: 7/7 match oracle '256\n7734030'
  > calc: cpp x15 ...
  > calc: rust x15 ...
  > calc: go x15 ...
  > calc: js x15 ...
  > calc: python x15 ...
  > calc: java x15 ...
  > calc: cp3 x15 ...
  > calc: goose x15 ...

  * calc  expected = 3750
1250
580458 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cp3         14.03    11.79   103.0  182.0      baseline      5.9      432
  rust        16.45    12.88   20.18  149.8  1.17x slower      4.7      843
  goose       20.06    10.95   22.95   84.6  1.43x slower      4.2       58
  go          21.64    16.30   24.05  152.0  1.54x slower      5.8     1754
  cpp         24.07    15.94   28.38   72.8  1.71x slower      4.7       54
  python      80.34    76.74   95.79    7.9  5.72x slower     11.4        -
  js          84.32    73.22   117.4   18.4  6.01x slower     43.9        -
  java        94.91    91.05   113.5   10.1  6.76x slower     41.7        2
    +  output verified: 8/8 match oracle '3750\n1250\n580458'
  > particles: cpp x15 ...
  > particles: rust x15 ...
  > particles: go x15 ...
  > particles: js x15 ...
  > particles: python x15 ...
  > particles: java x15 ...
  > particles: cp3 x15 ...
  > particles: goose x15 ...

  * particles  expected = 5000
28315536 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        15.75    14.01   22.14  149.8   1.02x faster      4.8      840
  cp3         16.11    14.40   92.70  144.2       baseline      5.3      426
  goose       17.79    13.39   22.67   74.5   1.10x slower      4.2       58
  go          18.16    12.96   27.55  202.9   1.13x slower      5.9     1751
  cpp         20.01    15.61   22.94   71.8   1.24x slower      4.7       45
  js          77.73    71.12   86.84    7.7   4.82x slower     41.0        -
  java        104.5    92.13   109.9    8.8   6.49x slower     38.0        1
  python      451.1    441.1   461.7    1.5  28.00x slower     12.0        -
    +  output verified: 8/8 match oracle '5000\n28315536'
  > push: cpp x15 ...
  > push: rust x15 ...
  > push: go x15 ...
  > push: js x15 ...
  > push: python x15 ...
  > push: java x15 ...
  > push: cp3 x15 ...
  - omit  push: goose: goose entry not authored for this task yet

  * push  expected = 1000000
15625
499758894813
750050 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         24.09    19.33   30.53   83.6       -     12.8       46
  rust        25.43    20.32   30.89   87.4       -     21.2      840
  go          28.98    23.45   41.10  111.7       -     22.9     1752
  java        111.0    100.9   128.4    9.1       -     82.0        1
  js          385.5    349.4   400.9    4.7       -    300.6        -
  python      712.0    695.7   755.9    4.0       -    114.1        -
  cp3             -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:13:1: error: CP3411: type mismatch: cannot mix signed 'i64' with unsigned 'u64';
           convert one side explicitly, e.g. parseTo(x, u64) to widen the signed side, or parseTo(x, i64) to narrow the unsigned
           side
    +  output verified: 6/6 match oracle '1000000\n15625\n499758894813\n750050'
  > records: cpp x15 ...
  > records: rust x15 ...
  > records: go x15 ...
  > records: js x15 ...
  > records: python x15 ...
  > records: java x15 ...
  > records: cp3 x15 ...
  > records: goose x15 ...

  * records  expected = 100000
17463411840 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cpp         17.73    16.07   23.39  142.2  2.92x faster      9.3       47
  goose       19.77    16.61   27.74  105.9  2.62x faster      7.8       58
  rust        21.75    16.50   26.12  145.5  2.38x faster     12.2      841
  go          23.72    17.37   32.72  160.0  2.18x faster     11.4     1752
  cp3         51.82    49.27   110.5   94.6      baseline     16.4      426
  java        86.40    82.55   98.22    7.8  1.67x slower     45.1        2
  js          120.9    115.5   128.2    4.8  2.33x slower     70.6        -
  python      228.7    221.9   236.3    3.0  4.41x slower     26.9        -
    +  output verified: 8/8 match oracle '100000\n17463411840'
  > scene: cpp x15 ...
  > scene: rust x15 ...
  > scene: go x15 ...
  > scene: js x15 ...
  > scene: python x15 ...
  > scene: java x15 ...
  > scene: cp3 x15 ...
  > scene: goose x15 ...

  * scene  expected = 23
4747 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        15.75    14.22   19.97  120.0  1.23x faster      4.7      842
  goose       16.35    14.44   21.21   70.1  1.18x faster      3.6       58
  cp3         19.36    13.12   89.34  169.8      baseline      5.3      428
  go          20.32    14.68   29.64  156.1  1.05x slower      5.9     1752
  cpp         20.49    14.46   24.53   75.7  1.06x slower      4.7       50
  python      36.40    33.38   44.19   16.6  1.88x slower     11.1        -
  js          66.82    61.81   80.87   10.2  3.45x slower     37.9        -
  java        74.64    65.88   93.11   16.5  3.85x slower     39.0        2
    +  output verified: 8/8 match oracle '23\n4747'
  > sexp: cpp x15 ...
  > sexp: rust x15 ...
  > sexp: go x15 ...
  > sexp: js x15 ...
  > sexp: python x15 ...
  > sexp: java x15 ...
  > sexp: cp3 x15 ...
  - omit  sexp: goose: goose entry not authored for this task yet

  * sexp  cross-check only (no oracle yet) | sexp | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        12.73    10.79   15.61  185.6  16.19x faster      4.7      841
  go          16.78    11.69   22.22  186.3  12.29x faster      6.0     1754
  cpp         22.03    17.55   26.45   95.2   9.36x faster      4.7       56
  java        82.40    67.13   117.7   23.6   2.50x faster     40.1        1
  js          88.84    74.28   109.6   11.8   2.32x faster     44.1        -
  python      102.2    95.87   113.1    6.3   2.02x faster     11.6        -
  cp3         206.1    126.6   320.8   30.4       baseline      6.5      425
    x  rust output '122590\n6764934848165057546'; expected '56938\n13287444230854067034'
  > respond: cpp x15 ...
  > respond: rust x15 ...
  > respond: go x15 ...
  > respond: js x15 ...
  > respond: python x15 ...
  > respond: java x15 ...
  > respond: cp3 x15 ...
  > respond: goose x15 ...

  * respond  expected = 578079
3348416444655930007 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  goose       15.24    11.52   21.65   81.7   7.55x faster      3.7       58
  go          16.35    14.32   33.66  167.9   7.04x faster     10.1     1756
  rust        16.96    13.52   27.42  110.4   6.79x faster      6.3      842
  cpp         19.37    13.69   28.06   90.0   5.94x faster      6.1       50
  cp3         115.1    86.77   353.6   69.9       baseline      6.5      432
  java        123.7    103.7   141.0   11.3   1.07x slower     43.7        2
  js          137.0    123.7   155.0    8.6   1.19x slower     59.9        -
  python     2032.4    962.3  2133.8   32.4  17.66x slower     13.5        -
    +  output verified: 8/8 match oracle '578079\n3348416444655930007'
  > sum-array: cpp x15 ...
  > sum-array: rust x15 ...
  > sum-array: go x15 ...
  > sum-array: js x15 ...
  > sum-array: python x15 ...
  > sum-array: java x15 ...
  > sum-array: cp3 x15 ...
  - omit  sum-array: goose: goose entry not authored for this task yet

  * sum-array  expected = 4000000
15987891712 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        31.56    30.15   34.88  108.1   1.75x faster     19.9      841
  cpp         32.30    30.26   40.32   47.5   1.71x faster     19.9       45
  go          48.63    38.11   75.28   73.7   1.14x faster     22.3     1750
  cp3         55.36    53.58   101.2   71.4       baseline     38.2      418
  java        111.9    108.1   117.4    4.0   2.02x slower     54.9        1
  js          620.9    610.2   634.0    1.6  11.22x slower     56.1        -
  python     2961.2   2942.5  2990.7    0.6  53.49x slower    159.3        -
    +  output verified: 7/7 match oracle '4000000\n15987891712'

  * performance gate  D64 target: cp3 within about 5% of cpp/rust (WARN over 1.05x); BIG WARN over 1.20x cpp/rust or slower tha..
  ------------------------------------------------------------------------------------------------------------
  ! BIG WARN  compute-single: cp3 453 ms is 2.19x cpp (206 ms), tripwire 1.20x
  ! BIG WARN  compute-single: cp3 453 ms is 2.41x rust (188 ms), tripwire 1.20x
  ! BIG WARN  compute-single: cp3 453 ms is 1.60x go (283 ms), tripwire 1.00x
  ! BIG WARN  compute-single: cp3 453 ms is 1.25x java (363 ms), tripwire 1.00x
  ! BIG WARN  compute-single-int: cp3 456 ms is 2.20x cpp (207 ms), tripwire 1.20x
  ! BIG WARN  compute-single-int: cp3 456 ms is 2.41x rust (189 ms), tripwire 1.20x
  ! BIG WARN  compute-single-int: cp3 456 ms is 1.64x go (279 ms), tripwire 1.00x
  ! BIG WARN  compute-single-int: cp3 456 ms is 1.22x java (374 ms), tripwire 1.00x
  ! WARN      hello-world: cp3 15 ms is 1.13x rust (14 ms), watch 1.05x
  ! BIG WARN  hello-world: cp3 15 ms is 1.08x go (14 ms), tripwire 1.00x
  ! BIG WARN  dev-reload: cp3 1655 ms is 2.54x cpp (652 ms), tripwire 1.20x
  ! BIG WARN  dev-reload: cp3 1655 ms is 3.48x rust (475 ms), tripwire 1.20x
  ! BIG WARN  dev-reload: cp3 1655 ms is 1.43x go (1157 ms), tripwire 1.00x
  ! BIG WARN  dev-reload: cp3 1655 ms is 26.93x js (61 ms), tripwire 1.00x
  ! BIG WARN  dev-reload: cp3 1655 ms is 45.10x python (37 ms), tripwire 1.00x
  ! BIG WARN  dev-reload: cp3 1655 ms is 3.05x java (542 ms), tripwire 1.00x
  ! BIG WARN  giant-repo-dev-reload: cp3 2272 ms is 3.04x cpp (747 ms), tripwire 1.20x
  ! BIG WARN  giant-repo-dev-reload: cp3 2272 ms is 3.42x rust (664 ms), tripwire 1.20x
  ! BIG WARN  giant-repo-dev-reload: cp3 2272 ms is 1.73x go (1311 ms), tripwire 1.00x
  ! BIG WARN  giant-repo-dev-reload: cp3 2272 ms is 6.17x js (368 ms), tripwire 1.00x
  ! BIG WARN  giant-repo-dev-reload: cp3 2272 ms is 6.66x python (341 ms), tripwire 1.00x
  ! BIG WARN  giant-repo-dev-reload: cp3 2272 ms is 1.40x java (1624 ms), tripwire 1.00x
  ! BIG WARN  buffer-copy: cp3 30 ms is 1.50x rust (20 ms), tripwire 1.20x
  ! BIG WARN  math-kernel: cp3 67 ms is 1.84x rust (37 ms), tripwire 1.20x
  ! BIG WARN  generics-mono: cp3 370 ms is 14.43x cpp (26 ms), tripwire 1.20x
  ! BIG WARN  generics-mono: cp3 370 ms is 11.51x rust (32 ms), tripwire 1.20x
  ! BIG WARN  generics-mono: cp3 370 ms is 10.79x go (34 ms), tripwire 1.00x
  ! BIG WARN  generics-mono: cp3 370 ms is 4.06x js (91 ms), tripwire 1.00x
  ! BIG WARN  generics-mono: cp3 370 ms is 2.94x java (126 ms), tripwire 1.00x
  ! BIG WARN  async-fanout: cp3 22 ms is 1.01x go (22 ms), tripwire 1.00x
  ! BIG WARN  path-join: cp3 268 ms is 1.39x rust (192 ms), tripwire 1.20x
  ! BIG WARN  path-join: cp3 268 ms is 2.90x go (92 ms), tripwire 1.00x
  ! BIG WARN  os-probe: cp3 243 ms is 1.21x java (201 ms), tripwire 1.00x
  ! BIG WARN  binary-trees: cp3 346 ms is 5.07x cpp (68 ms), tripwire 1.20x
  ! BIG WARN  binary-trees: cp3 346 ms is 8.43x rust (41 ms), tripwire 1.20x
  ! BIG WARN  binary-trees: cp3 346 ms is 5.30x go (65 ms), tripwire 1.00x
  ! BIG WARN  binary-trees: cp3 346 ms is 4.14x js (84 ms), tripwire 1.00x
  ! BIG WARN  binary-trees: cp3 346 ms is 1.24x python (279 ms), tripwire 1.00x
  ! BIG WARN  binary-trees: cp3 346 ms is 3.95x java (88 ms), tripwire 1.00x
  ! BIG WARN  exception-cost: cp3 1152 ms is 1.96x cpp (589 ms), tripwire 1.20x
  ! BIG WARN  exception-cost: cp3 1152 ms is 2.92x python (394 ms), tripwire 1.00x
  ! BIG WARN  exception-cost: cp3 1152 ms is 4.48x java (257 ms), tripwire 1.00x
  ! BIG WARN  tree-walk: cp3 23 ms is 1.24x cpp (19 ms), tripwire 1.20x
  ! BIG WARN  tree-walk: cp3 23 ms is 1.41x rust (17 ms), tripwire 1.20x
  ! BIG WARN  tree-walk: cp3 23 ms is 1.26x go (19 ms), tripwire 1.00x
  ! BIG WARN  graph-bfs: cp3 71 ms is 1.77x cpp (40 ms), tripwire 1.20x
  ! BIG WARN  graph-bfs: cp3 71 ms is 2.01x rust (35 ms), tripwire 1.20x
  ! BIG WARN  graph-bfs: cp3 71 ms is 2.24x go (32 ms), tripwire 1.00x
  ! BIG WARN  strlist: cp3 81 ms is 3.10x cpp (26 ms), tripwire 1.20x
  ! BIG WARN  strlist: cp3 81 ms is 2.92x rust (28 ms), tripwire 1.20x
  ! BIG WARN  strlist: cp3 81 ms is 2.30x go (35 ms), tripwire 1.00x
  ! WARN      blur: cp3 19 ms is 1.11x cpp (17 ms), watch 1.05x
  ! WARN      blur: cp3 19 ms is 1.09x rust (17 ms), watch 1.05x
  ! BIG WARN  records: cp3 52 ms is 2.92x cpp (18 ms), tripwire 1.20x
  ! BIG WARN  records: cp3 52 ms is 2.38x rust (22 ms), tripwire 1.20x
  ! BIG WARN  records: cp3 52 ms is 2.18x go (24 ms), tripwire 1.00x
  ! BIG WARN  scene: cp3 19 ms is 1.23x rust (16 ms), tripwire 1.20x
  ! BIG WARN  sexp: cp3 206 ms is 9.36x cpp (22 ms), tripwire 1.20x
  ! BIG WARN  sexp: cp3 206 ms is 16.19x rust (13 ms), tripwire 1.20x
  ! BIG WARN  sexp: cp3 206 ms is 12.29x go (17 ms), tripwire 1.00x
  ! BIG WARN  sexp: cp3 206 ms is 2.32x js (89 ms), tripwire 1.00x
  ! BIG WARN  sexp: cp3 206 ms is 2.02x python (102 ms), tripwire 1.00x
  ! BIG WARN  sexp: cp3 206 ms is 2.50x java (82 ms), tripwire 1.00x
  ! BIG WARN  respond: cp3 115 ms is 5.94x cpp (19 ms), tripwire 1.20x
  ! BIG WARN  respond: cp3 115 ms is 6.79x rust (17 ms), tripwire 1.20x
  ! BIG WARN  respond: cp3 115 ms is 7.04x go (16 ms), tripwire 1.00x
  ! BIG WARN  sum-array: cp3 55 ms is 1.71x cpp (32 ms), tripwire 1.20x
  ! BIG WARN  sum-array: cp3 55 ms is 1.75x rust (32 ms), tripwire 1.20x
  ! BIG WARN  sum-array: cp3 55 ms is 1.14x go (49 ms), tripwire 1.00x
  ------------------------------------------------------------------------------------------------------------
  ! gate: 66 BIG WARN(s), 3 WARN(s); rerun with --strict to fail on warnings; 21 row(s) did not run
```
<!-- latest-full-run:end -->
