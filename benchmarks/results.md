

## Latest results by test

<!-- latest-by-test:start -->
<!-- test:health-server:start -->
### health-server
_Updated 2026-11-13T12:15:20 by `python benchmarks/runner/run_benchmark.py`._

```text
  * health-server  median boot/req/s; pooled latency p50/p95; CV = run-to-run variation %
  lang        ok/run  boot ms  p50 ms  p95 ms   req/s  RPS CV%  errors         vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------------------------------------------
  java           1/1   1146.0    0.30    0.55   9,823        -       0   1.23x faster    171.6        4
  cp3            1/1    545.6    0.29    0.45   7,955        -       0       baseline     17.7     7494
  go             1/1    537.2    0.13    0.47   7,585        -       0   1.05x slower     70.0    15630
  rust           1/1    520.8    0.50    0.82   7,122        -       0   1.12x slower      8.9     2154
  cpp            1/1    101.3    0.17    0.65   7,013        -       0   1.13x slower      6.3       60
  dotnet         1/1    588.7    0.21    0.64   6,852        -       0   1.16x slower    212.8        8
  js-fastify     1/1   1563.0    0.16    0.40   5,280        -       0   1.51x slower     69.6        -
  js             1/1   2572.2    0.27    0.57   3,001        -       0   2.65x slower     65.4        -
  python         1/1   2087.6    0.60    1.04     502        -       0  15.85x slower      4.1        -
  goose          0/1        -       -       -  no run        -       0              -        -        -

gate: BIG WARN (fix this): health-server: cp3 7955 req/s is 1.23x slower than java (9823 req/s), tripwire 1.00x
```
<!-- test:health-server:end -->

<!-- test:health-server-multi:start -->
### health-server-multi
_Updated 2026-11-13T12:19:01 by `python benchmarks/runner/run_benchmark.py`._

```text
  * health-server-multi  median boot/req/s; pooled latency p50/p95; CV = run-to-run variation %
  lang        ok/run  boot ms  p50 ms  p95 ms   req/s  RPS CV%  errors         vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------------------------------------------
  cp3            1/1    517.3    0.21    0.47  64,797        -       0       baseline     22.9     7498
  java           1/1   1121.6    0.35    0.61  60,988        -       0   1.06x slower    362.4        4
  cpp            1/1    520.8    0.11    0.54  48,085        -      12   1.35x slower      6.4       61
  js-fastify     1/1   2078.6    0.37    0.56  37,118        -       0   1.75x slower     54.4        -
  go             1/1    517.6    0.34    0.58  35,556        -       0   1.82x slower     72.5    15630
  dotnet         1/1    546.9    0.35    0.63  25,361        -       0   2.55x slower    350.1        9
  js             1/1   1567.3    0.26    0.55  21,288        -       0   3.04x slower     50.3        -
  rust           1/1    522.9    0.50    0.70  19,225        -      28   3.37x slower     10.0     2154
  python         1/1   3145.4    1.00    1.63   5,947        -       0  10.90x slower      4.0        -
  goose          0/1        -       -       -  no run        -       0              -        -        -
```
<!-- test:health-server-multi:end -->

<!-- test:compute-single:start -->
### compute-single
_Updated 2026-11-13T12:25:44 by `python benchmarks/runner/run_benchmark.py`._

```text
  * compute-single  expected = 131434424 (total Collatz steps for 1..1,000,000) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms   p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        205.2    193.2    260.6  30.8   2.36x faster      4.5      122
  cpp         262.7    213.8    321.6  33.7   1.84x faster      5.1       19
  go          324.0    288.6    501.4  32.0   1.49x faster      6.1     1750
  java        405.6    388.6    426.6   4.3   1.19x faster     39.2        1
  cp3         483.3    477.0    642.0  16.7       baseline      5.7      414
  js         1605.1   1581.2   1676.3   2.4   3.32x slower     38.3        -
  python    22707.3  20630.6  23571.9   9.2  46.98x slower     10.9        -
  goose           -        -        -     -              -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\compute-single\goose\main.goose
    +  output verified: 7/7 match oracle '131434424'

gate: BIG WARN (fix this): compute-single: cp3 483 ms is 1.84x cpp (263 ms), tripwire 1.20x
gate: BIG WARN (fix this): compute-single: cp3 483 ms is 2.36x rust (205 ms), tripwire 1.20x
gate: BIG WARN (fix this): compute-single: cp3 483 ms is 1.49x go (324 ms), tripwire 1.00x
gate: BIG WARN (fix this): compute-single: cp3 483 ms is 1.19x java (406 ms), tripwire 1.00x
```
<!-- test:compute-single:end -->

<!-- test:compute-single-int:start -->
### compute-single-int
_Updated 2026-11-13T12:33:38 by `python benchmarks/runner/run_benchmark.py`._

```text
  * compute-single-int  expected = 131434424 (total Collatz steps for 1..1,000,000) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms   p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        196.4    189.2    212.2  29.5   2.33x faster      4.1      123
  cpp         225.0    212.0    232.1  13.2   2.03x faster      4.6       19
  go          292.3    286.0    295.3  30.1   1.57x faster      5.9     1750
  java        371.3    360.6    378.6   1.5   1.23x faster     38.9        1
  cp3         457.8    451.8    594.1  19.1       baseline      5.7      414
  js         1590.9   1565.6   1610.7   1.0   3.47x slower     45.4        -
  python    28326.3  26193.4  28890.8   2.8  61.87x slower     10.9        -
  goose           -        -        -     -              -        -        -
    x goose  build failed: cannot open file:
             C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\compute-single-int\goose\main.goose
    +  output verified: 7/7 match oracle '131434424'

gate: BIG WARN (fix this): compute-single-int: cp3 458 ms is 2.03x cpp (225 ms), tripwire 1.20x
gate: BIG WARN (fix this): compute-single-int: cp3 458 ms is 2.33x rust (196 ms), tripwire 1.20x
gate: BIG WARN (fix this): compute-single-int: cp3 458 ms is 1.57x go (292 ms), tripwire 1.00x
gate: BIG WARN (fix this): compute-single-int: cp3 458 ms is 1.23x java (371 ms), tripwire 1.00x
```
<!-- test:compute-single-int:end -->

<!-- test:compute-multi:start -->
### compute-multi
_Updated 2026-11-13T12:34:13 by `python benchmarks/runner/run_benchmark.py`._

```text
  * compute-multi  expected = 131434424 (same total split across all cores) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  rust        37.75    26.02   46.92   87.5       -      4.6      154
  cpp         43.09    32.58   65.83   59.5       -      5.4       21
  go          54.72    45.23   71.67  130.8       -      7.1     1759
  java        136.7    123.2   171.9   12.2       -     41.4        1
  js          325.9    298.2   347.8   10.8       -    283.7        -
  python     1533.9   1463.0  1640.0    4.2       -     19.7        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:34:1: error: CP3401: type mismatch: cannot assign 'number' to 'i64'
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\compute-multi\goose\main.goose
    +  output verified: 6/6 match oracle '131434424'
```
<!-- test:compute-multi:end -->

<!-- test:hello-world:start -->
### hello-world
_Updated 2026-11-13T12:34:24 by `python benchmarks/runner/run_benchmark.py`._

```text
  * hello-world  expected = Hello, world! | spawn -> exit wall time; 15 runs | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cpp         16.25    11.33   29.17   99.8  1.56x faster      4.6       19
  go          17.10    11.77   26.19  231.8  1.48x faster      5.7     1750
  rust        17.30    11.63   24.97  121.9  1.47x faster      4.1      122
  cp3         25.39    18.25   260.1  162.8      baseline      5.2      410
  python      36.88    32.95   44.73   22.3  1.45x slower     10.8        -
  js          63.52    57.34   67.33    5.9  2.50x slower     35.2        -
  java        70.36    63.28   80.65   10.5  2.77x slower     38.8        0
  goose           -        -       -      -             -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\hello-world\goose\main.goose
    +  output verified: 7/7 match oracle 'Hello, world!'

gate: BIG WARN (fix this): hello-world: cp3 25 ms is 1.56x cpp (16 ms), tripwire 1.20x
gate: BIG WARN (fix this): hello-world: cp3 25 ms is 1.47x rust (17 ms), tripwire 1.20x
gate: BIG WARN (fix this): hello-world: cp3 25 ms is 1.48x go (17 ms), tripwire 1.00x
```
<!-- test:hello-world:end -->

<!-- test:dev-reload:start -->
### dev-reload
_Updated 2026-11-13T19:50:26 by `python benchmarks/runner/run_benchmark.py --only dev-reload --reps 5 --cp3c build\windows-release\bin\cp3c.exe`._

```text
  ◆ dev-reload  expected = Hello, world! | one edit -> build -> rerun cycle; median/best/p95 across 5 alternating edits | median…
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  python      50.53    42.93   61.29  16.7  25.60x faster        -        -
  js          102.3    73.96   112.6  16.7  12.64x faster        -        -
  java        539.7    527.2   584.1   4.1   2.40x faster        -        -
  rust        732.0    724.7  1092.8  20.2   1.77x faster        -        -
  cpp         747.2    722.8   766.4   2.6   1.73x faster        -        -
  cp3        1293.5   1261.1  3181.6  51.0       baseline        -        -
  go         1939.8    478.3  1993.2  39.8   1.50x slower        -        -
    ✓  output verified: 7/7 match oracle 'Hello, world!'

gate: BIG WARN (fix this): dev-reload: cp3 1294 ms is 1.73x cpp (747 ms), tripwire 1.20x
gate: BIG WARN (fix this): dev-reload: cp3 1294 ms is 1.77x rust (732 ms), tripwire 1.20x
gate: BIG WARN (fix this): dev-reload: cp3 1294 ms is 12.64x js (102 ms), tripwire 1.00x
gate: BIG WARN (fix this): dev-reload: cp3 1294 ms is 25.60x python (51 ms), tripwire 1.00x
gate: BIG WARN (fix this): dev-reload: cp3 1294 ms is 2.40x java (540 ms), tripwire 1.00x
```
<!-- test:dev-reload:end -->

<!-- test:giant-repo-dev-reload:start -->
### giant-repo-dev-reload
_Updated 2026-11-13T20:04:05 by `python benchmarks/runner/run_benchmark.py --only giant-repo-dev-reload --reps 1 --cp3c build\windows-release\bin\cp3c.exe`._

```text
  ◆ giant-repo-dev-reload  expected = Hello, world! | one edit -> build -> rerun cycle; median/best/p95 across 1 alternating edi…
  lang    median ms  best ms   p95 ms  CV%         vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  js          440.0    440.0    440.0    -  75.97x faster        -        -
  java       1650.2   1650.2   1650.2    -  20.25x faster        -        -
  go         2846.7   2846.7   2846.7    -  11.74x faster        -        -
  cpp        3616.2   3616.2   3616.2    -   9.24x faster        -        -
  rust       3900.8   3900.8   3900.8    -   8.57x faster        -        -
  python     3916.9   3916.9   3916.9    -   8.53x faster        -        -
  cp3       33423.2  33423.2  33423.2    -       baseline        -        -
    ✓  output verified: 7/7 match oracle 'Hello, world!'

gate: BIG WARN (fix this): giant-repo-dev-reload: cp3 33423 ms is 9.24x cpp (3616 ms), tripwire 1.20x
gate: BIG WARN (fix this): giant-repo-dev-reload: cp3 33423 ms is 8.57x rust (3901 ms), tripwire 1.20x
gate: BIG WARN (fix this): giant-repo-dev-reload: cp3 33423 ms is 11.74x go (2847 ms), tripwire 1.00x
gate: BIG WARN (fix this): giant-repo-dev-reload: cp3 33423 ms is 75.97x js (440 ms), tripwire 1.00x
gate: BIG WARN (fix this): giant-repo-dev-reload: cp3 33423 ms is 8.53x python (3917 ms), tripwire 1.00x
gate: BIG WARN (fix this): giant-repo-dev-reload: cp3 33423 ms is 20.25x java (1650 ms), tripwire 1.00x
```
<!-- test:giant-repo-dev-reload:end -->

<!-- test:string-churn:start -->
### string-churn
_Updated 2026-11-13T12:40:56 by `python benchmarks/runner/run_benchmark.py`._

```text
  * string-churn  expected = 852167 | scale: default (50000 iterations) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cp3         324.1    318.4   481.0  23.4       baseline      5.5      428
  rust        353.1    348.2   359.4  36.6   1.09x slower      4.5      166
  python     2028.8   1978.6  2071.8   1.5   6.26x slower     11.0        -
  cpp        2207.3   2135.9  2278.4   2.5   6.81x slower      5.0       23
  js         2657.6   2588.1  2743.4   2.1   8.20x slower     46.4        -
  java       2727.7   2627.6  2760.5   1.3   8.42x slower    653.5        2
  go         5787.3   4941.0  6255.3  17.0  17.86x slower     98.3     1765
  goose           -        -       -     -              -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-string\string-churn\goose\main.goose
    +  output verified: 7/7 match oracle '852167'
```
<!-- test:string-churn:end -->

<!-- test:sha256-churn:start -->
### sha256-churn
_Updated 2026-11-13T12:41:11 by `python benchmarks/runner/run_benchmark.py`._

```text
  * sha256-churn  expected = 218229 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cp3         28.70    20.37   124.2  158.7      baseline      6.2      674
  go          62.40    56.83   71.63  117.8  2.17x slower      6.1     1922
  python      136.0    133.8   141.3    5.5  4.74x slower     13.3        -
  js          147.5    139.5   156.7    4.2  5.14x slower     41.9        -
  java        171.1    160.7   181.1    3.5  5.96x slower     49.6        1
  goose           -        -       -      -             -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\sha256-churn\goose\main.goose
    +  output verified: 5/5 match oracle '218229'
```
<!-- test:sha256-churn:end -->

<!-- test:buffer-copy:start -->
### buffer-copy
_Updated 2026-11-13T12:41:30 by `python benchmarks/runner/run_benchmark.py`._

```text
  * buffer-copy  expected = 688024 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        29.53    24.49   38.26  183.8  1.11x faster      4.3      125
  cp3         32.67    26.57   183.2  136.5      baseline      5.9      426
  python      71.30    64.42   97.49   16.9  2.18x slower     11.7        -
  go          74.05    62.64   81.14  103.4  2.27x slower     76.2     1752
  cpp         106.6    23.15   135.3   63.3  3.26x slower      4.8       21
  java        173.2    161.8   237.2   15.2  5.30x slower    264.2        1
  js          255.6    235.9   263.8    4.5  7.82x slower    116.1        -
  goose           -        -       -      -             -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-array\buffer-copy\goose\main.goose
    +  output verified: 7/7 match oracle '688024'

gate: WARN buffer-copy: cp3 33 ms is 1.11x rust (30 ms), watch 1.05x
```
<!-- test:buffer-copy:end -->

<!-- test:typed-array-sum:start -->
### typed-array-sum
_Updated 2026-11-13T12:42:03 by `python benchmarks/runner/run_benchmark.py`._

```text
  * typed-array-sum  expected = 135618465 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  rust        35.77    33.51   43.68  233.1       -     12.7      128
  cpp         37.62    31.89   54.00   59.4       -     13.2       22
  go          48.30    38.09   62.06  135.9       -     15.7     1750
  js          100.7    94.47   135.1   14.0       -     49.2        -
  java        127.8    122.8   132.9    2.8       -     48.4        1
  python     1530.7   1502.7  1546.9    1.2       -     20.1        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:51:1: error: CP3401: type mismatch: cannot assign 'number' to 'i64'
    x goose  build failed: cannot open file:
             C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-array\typed-array-sum\goose\main.goose
    +  output verified: 6/6 match oracle '135618465'
```
<!-- test:typed-array-sum:end -->

<!-- test:math-kernel:start -->
### math-kernel
_Updated 2026-11-13T12:43:04 by `python benchmarks/runner/run_benchmark.py`._

```text
  * math-kernel  expected = 177047762 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  rust        42.75    38.41   45.89  191.4       -      4.1      124
  cpp         117.6    114.5   121.3   19.6       -      4.6       20
  go          125.0    117.8   145.9   69.7       -      6.1     1762
  js          158.7    152.3   190.5    9.0       -     40.1        -
  java        175.2    165.4   194.5    4.5       -     39.3        1
  python     3074.2   2967.3  3486.3    6.4       -     11.0        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:36:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\math-kernel\goose\main.goose
    +  output verified: 6/6 match oracle '177047762'
```
<!-- test:math-kernel:end -->

<!-- test:word-frequency:start -->
### word-frequency
_Updated 2026-11-13T12:43:33 by `python benchmarks/runner/run_benchmark.py`._

```text
  * word-frequency  expected = 37425343 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  go          54.67    45.15   65.75  123.5  1.99x faster      6.0     1762
  rust        80.64    67.47   115.6   39.1  1.35x faster      4.4      130
  cpp         85.03    67.70   127.1   29.7  1.28x faster      5.2       23
  cp3         109.1    104.4   264.9   69.4      baseline      6.2      711
  java        163.6    156.3   167.4    2.2  1.50x slower     72.5        1
  js          249.3    241.7   261.0    3.7  2.29x slower     38.9        -
  python      693.5    672.8   709.6    1.4  6.36x slower     11.0        -
  goose           -        -       -      -             -        -        -
    x goose  build failed: cannot open file:
             C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-string\word-frequency\goose\main.goose
    +  output verified: 7/7 match oracle '37425343'

gate: BIG WARN (fix this): word-frequency: cp3 109 ms is 1.28x cpp (85 ms), tripwire 1.20x
gate: BIG WARN (fix this): word-frequency: cp3 109 ms is 1.35x rust (81 ms), tripwire 1.20x
gate: BIG WARN (fix this): word-frequency: cp3 109 ms is 1.99x go (55 ms), tripwire 1.00x
```
<!-- test:word-frequency:end -->

<!-- test:object-churn:start -->
### object-churn
_Updated 2026-11-13T12:44:28 by `python benchmarks/runner/run_benchmark.py`._

```text
  * object-churn  expected = 90641269 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  go          75.43    71.25   80.33  95.2       -     46.6     1751
  cpp         157.1    151.8   183.4  13.5       -     62.4       20
  rust        161.7    156.9   174.7  58.8       -     61.9      124
  java        189.3    178.9   222.2   7.4       -    132.6        1
  js          450.9    431.9   507.2   5.0       -    307.1        -
  python     1978.2   1925.3  2110.4  10.0       -    233.9        -
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet:
           C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-object\object-churn\cp3\.cp3\build\src/index.cpp:265:17: error: no
           matching function for call to 'arrayPush'
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-object\object-churn\goose\main.goose
    +  output verified: 6/6 match oracle '90641269'
```
<!-- test:object-churn:end -->

<!-- test:array-growth:start -->
### array-growth
_Updated 2026-11-13T12:45:26 by `python benchmarks/runner/run_benchmark.py`._

```text
  * array-growth  expected = 200000010000000 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cp3         64.30    59.15   211.4  86.8       baseline    157.9      414
  rust        147.7    134.2   178.6  19.0   2.30x slower    261.1      124
  cpp         155.2    140.5   176.5  14.9   2.41x slower    261.4       20
  java        295.9    271.9   305.7   3.7   4.60x slower    554.9        1
  go          306.2    273.6   340.7  29.2   4.76x slower    625.1     1750
  js          423.9    403.3   452.6   5.2   6.59x slower    363.3        -
  python     1975.2   1927.1  2028.2   1.7  30.72x slower    832.0        -
  goose           -        -       -     -              -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-array\array-growth\goose\main.goose
    +  output verified: 7/7 match oracle '200000010000000'
```
<!-- test:array-growth:end -->

<!-- test:closure-capture:start -->
### closure-capture
_Updated 2026-11-13T12:46:15 by `python benchmarks/runner/run_benchmark.py`._

```text
  * closure-capture  expected = 16666688333331 998521500000 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        24.45    20.11   28.51  187.6   2.44x faster      4.1      125
  go          26.91    20.68   32.97  195.9   2.21x faster      6.0     1752
  cp3         59.53    52.76   189.0   92.9       baseline     81.7      420
  cpp         106.2    89.40   111.6   25.4   1.78x slower    182.8       21
  java        157.2    139.2   173.1    5.5   2.64x slower     78.6        2
  js          852.4    817.0   928.3    4.5  14.32x slower    293.1        -
  python     1512.2   1469.1  1573.9    2.0  25.40x slower     11.3        -
  goose           -        -       -      -              -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\closure-capture\goose\main.goose
    +  output verified: 7/7 match oracle '16666688333331 998521500000'

gate: BIG WARN (fix this): closure-capture: cp3 60 ms is 2.44x rust (24 ms), tripwire 1.20x
gate: BIG WARN (fix this): closure-capture: cp3 60 ms is 2.21x go (27 ms), tripwire 1.00x
```
<!-- test:closure-capture:end -->

<!-- test:interface-dispatch:start -->
### interface-dispatch
_Updated 2026-11-13T12:47:04 by `python benchmarks/runner/run_benchmark.py`._

```text
  * interface-dispatch  expected = 37999986 66000000 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  go          59.67    49.66   64.95  119.3       -     63.6     1754
  rust        134.6    131.0   139.2  101.5       -     90.6      126
  cpp         158.3    151.8   163.8   17.5       -    111.5       22
  java        216.3    178.7   248.7   11.5       -    155.1        2
  js          580.5    544.2   596.5    3.0       -    388.7        -
  python     1821.1   1639.5  1974.0    6.9       -    237.2        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:79:1: error: CP3401: type mismatch: cannot assign 'number' to 'i64'
    x goose  build failed: cannot open file:
             C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\interface-dispatch\goose\main.goose
    +  output verified: 6/6 match oracle '37999986 66000000'
```
<!-- test:interface-dispatch:end -->

<!-- test:generics-mono:start -->
### generics-mono
_Updated 2026-11-13T12:47:22 by `python benchmarks/runner/run_benchmark.py`._

```text
  * generics-mono  expected = 998986007 998986007 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         32.34    26.26   44.93   77.4       -      4.6       19
  rust        33.26    28.98   56.64   89.5       -      4.1      124
  go          35.37    31.70   125.7  127.3       -      6.1     1750
  js          104.1    102.6   132.6   13.9       -     40.4        -
  java        137.6    122.5   173.6   13.6       -     51.9        2
  python      651.4    628.9   673.3    3.0       -     11.0        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:49:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\generics-mono\goose\main.goose
    +  output verified: 6/6 match oracle '998986007 998986007'
```
<!-- test:generics-mono:end -->

<!-- test:json-roundtrip:start -->
### json-roundtrip
_Updated 2026-11-13T12:47:37 by `python benchmarks/runner/run_benchmark.py`._

```text
  * json-roundtrip  expected = 664346 | 20,000 parse+stringify round trips of one 310-byte record | median/best/p95 ms; CV = va..
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  js          130.3    121.4   145.1   5.2       -     40.6        -
  go          164.2    157.4   200.0  54.0       -     12.8     2154
  java        264.3    257.8   279.0   2.7       -    155.6        4
  python      320.2    309.2   331.5   2.6       -     12.6        -
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:79:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\json-roundtrip\goose\main.goose
    +  output verified: 4/4 match oracle '664346'
```
<!-- test:json-roundtrip:end -->

<!-- test:async-fanout:start -->
### async-fanout
_Updated 2026-11-13T12:48:30 by `python benchmarks/runner/run_benchmark.py`._

```text
  * async-fanout  expected = 99898 | 10,000-timer fan-out joined at once, then 10,000 chained awaits | median/best/p95 ms; CV =..
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  go          23.03    21.62   25.22  195.3   1.02x faster     11.9     1760
  cp3         23.61    18.04   209.4  167.4       baseline      9.6      814
  js          97.60    87.34   106.8    9.1   4.13x slower     51.1        -
  java        220.0    201.1   251.1    6.9   9.32x slower     99.0        3
  python      262.3    252.3   341.2   11.1  11.11x slower     35.0        -
  goose           -        -       -      -              -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\async-fanout\goose\main.goose
    +  output verified: 5/5 match oracle '99898'

gate: BIG WARN (fix this): async-fanout: cp3 24 ms is 1.02x go (23 ms), tripwire 1.00x
```
<!-- test:async-fanout:end -->

<!-- test:env-injection:start -->
### env-injection
_Updated 2026-11-13T12:48:53 by `python benchmarks/runner/run_benchmark.py`._

```text
  * env-injection  expected = 133644 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  java        131.1    126.3   150.4   6.2       -     62.1        1
  rust        144.8    141.5   264.4  26.4       -      4.1      126
  cpp         170.9    161.8   216.6  33.5       -      4.6       21
  go          185.8    173.2   256.5  42.9       -     12.2     1755
  js          348.5    342.0   355.4   2.2       -     40.3        -
  python      391.8    383.5   399.0   1.6       -     11.0        -
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:37:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\env-injection\goose\main.goose
    +  output verified: 6/6 match oracle '133644'
```
<!-- test:env-injection:end -->

<!-- test:file-roundtrip:start -->
### file-roundtrip
_Updated 2026-11-13T12:49:32 by `python benchmarks/runner/run_benchmark.py`._

```text
  * file-roundtrip  expected = 698140 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  java        258.9    249.0   323.3  10.6       -     43.9        2
  rust        340.9    318.1   396.7  11.7       -      4.2      148
  cpp         372.4    340.5   517.5  14.9       -      5.2       33
  go          386.2    357.6   517.2  28.0       -      8.1     1900
  js          416.6    366.2   455.8   7.1       -     40.7        -
  python      467.5    431.2   835.5  37.6       -     13.8        -
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:53:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\file-roundtrip\goose\main.goose
    +  output verified: 6/6 match oracle '698140'
```
<!-- test:file-roundtrip:end -->

<!-- test:path-join:start -->
### path-join
_Updated 2026-11-13T12:50:14 by `python benchmarks/runner/run_benchmark.py`._

```text
  * path-join  expected = 425577 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  go          79.68    74.70   84.29  86.2       -     13.7     1768
  rust        203.5    197.2   217.9  79.4       -      4.2      135
  java        331.6    319.3   360.6   4.2       -    150.6        1
  js          462.6    437.6   509.3   7.0       -     42.2        -
  cpp         563.8    552.6   630.8   5.1       -      5.4       31
  python      833.8    722.1   932.0   6.2       -     10.9        -
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:30:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\path-join\goose\main.goose
    +  output verified: 6/6 match oracle '425577'
```
<!-- test:path-join:end -->

<!-- test:date-format:start -->
### date-format
_Updated 2026-11-13T12:50:23 by `python benchmarks/runner/run_benchmark.py`._

```text
  * date-format  expected = 796897 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         37.18    33.59   42.46   74.4       -      4.7       47
  go          37.35    31.81   49.11  163.5       -      7.5     1815
  js          96.89    90.44   103.2    5.8       -     40.6        -
  java        111.1    108.3   141.0   11.8       -     55.5        2
  python      200.7    194.9   206.5    4.0       -     11.1        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:79:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\date-format\goose\main.goose
    +  output verified: 5/5 match oracle '796897'
```
<!-- test:date-format:end -->

<!-- test:os-probe:start -->
### os-probe
_Updated 2026-11-13T12:51:22 by `python benchmarks/runner/run_benchmark.py`._

```text
  * os-probe  expected = 208981 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  java        204.7    194.4   228.8   6.3       -     43.2        2
  cpp         239.2    233.1   263.3  10.8       -      4.7       21
  go          506.5    498.0   860.9  36.8       -     12.2     1758
  python      635.5    628.4   657.7   1.6       -     14.1        -
  js         2048.5   1935.0  2183.1   8.2       -     42.1        -
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:43:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\os-probe\goose\main.goose
    +  output verified: 5/5 match oracle '208981'
```
<!-- test:os-probe:end -->

<!-- test:binary-trees:start -->
### binary-trees
_Updated 2026-11-13T12:51:52 by `python benchmarks/runner/run_benchmark.py`._

```text
  * binary-trees  expected = 1348958 | full binary trees to depth 13, recursive build and check | median/best/p95 ms; CV = vari..
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        48.60    40.73   59.50  134.8  7.27x faster      4.6      124
  cpp         70.73    67.82   233.3   71.3  4.99x faster      5.7       34
  go          74.44    48.61  2095.7  198.9  4.74x faster     13.3     1751
  java        87.08    80.26   92.00    5.1  4.06x faster     60.4        1
  js          96.23    80.61   109.1    9.0  3.67x faster     52.3        -
  python      280.5    277.2   295.8    3.5  1.26x faster     12.5        -
  cp3         353.2    347.1   489.3   23.3      baseline      9.0      420
  goose           -        -       -      -             -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-object\binary-trees\goose\main.goose
    +  output verified: 7/7 match oracle '1348958'

gate: BIG WARN (fix this): binary-trees: cp3 353 ms is 4.99x cpp (71 ms), tripwire 1.20x
gate: BIG WARN (fix this): binary-trees: cp3 353 ms is 7.27x rust (49 ms), tripwire 1.20x
gate: BIG WARN (fix this): binary-trees: cp3 353 ms is 4.74x go (74 ms), tripwire 1.00x
gate: BIG WARN (fix this): binary-trees: cp3 353 ms is 3.67x js (96 ms), tripwire 1.00x
gate: BIG WARN (fix this): binary-trees: cp3 353 ms is 1.26x python (281 ms), tripwire 1.00x
gate: BIG WARN (fix this): binary-trees: cp3 353 ms is 4.06x java (87 ms), tripwire 1.00x
```
<!-- test:binary-trees:end -->

<!-- test:map-churn:start -->
### map-churn
_Updated 2026-11-13T12:51:59 by `python benchmarks/runner/run_benchmark.py`._

```text
  * map-churn  expected = 942471769 | 100,000-key hash map, delete/reinsert/update churn | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         22.03    20.40   31.94   79.0       -      9.0       21
  rust        22.62    21.25   32.05   85.8       -      6.4      128
  go          24.65    23.75   26.51  196.4       -      8.2     1756
  python      69.79    68.04   75.17    7.2       -     23.8        -
  js          87.48    79.06   103.2   11.1       -     47.0        -
  java        109.8    107.6   141.3   10.2       -     70.9        1
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:39:1: error: CP3401: type mismatch: cannot assign 'number' to 'i64'
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-map\map-churn\goose\main.goose
    +  output verified: 6/6 match oracle '942471769'
```
<!-- test:map-churn:end -->

<!-- test:utf8-text:start -->
### utf8-text
_Updated 2026-11-13T12:52:43 by `python benchmarks/runner/run_benchmark.py`._

```text
  * utf8-text  expected = 78156718 | 417,792-byte UTF-8 text, 32 manual decode rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         50.25    44.73   81.89   41.3       -      5.1       21
  go          52.93    49.34   59.97  124.5       -      6.4     1756
  rust        54.15    49.20   55.50  129.1       -      4.5      124
  java        125.3    118.1   133.1    4.6       -     42.2        2
  js          196.9    193.0   217.6    4.3       -     40.8        -
  python     2188.5   2174.1  2263.4    1.4       -     12.4        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:62:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-string\utf8-text\goose\main.goose
    +  output verified: 6/6 match oracle '78156718'
```
<!-- test:utf8-text:end -->

<!-- test:regex-scan:start -->
### regex-scan
_Updated 2026-11-13T12:52:53 by `python benchmarks/runner/run_benchmark.py`._

```text
  * regex-scan  expected = 735703090 | 240,000-byte text, global [0-9]+ scan, 8 rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  js          84.75    75.73   90.16   5.9       -     42.4        -
  go          99.29    93.07   103.0  77.3       -     12.1     2074
  python      99.37    94.63   122.8  10.1       -     15.3        -
  cpp         127.3    122.9   133.9  33.7       -      5.1      130
  java        140.2    123.8   143.7   8.4       -     56.2        1
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:41:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-string\regex-scan\goose\main.goose
    +  output verified: 5/5 match oracle '735703090'
```
<!-- test:regex-scan:end -->

<!-- test:json-parse-large:start -->
### json-parse-large
_Updated 2026-11-13T12:53:03 by `python benchmarks/runner/run_benchmark.py`._

```text
  * json-parse-large  expected = 613781 | 204,472-byte JSON record, 20 parse rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  js          92.86    86.94   99.41   3.9       -     52.0        -
  python      127.7    125.0   132.7   5.9       -     16.6        -
  go          129.0    123.2   144.6  61.2       -     16.8     2154
  java        190.6    183.7   213.4   6.5       -    132.3        3
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:79:1: error: CP3401: type mismatch: cannot assign 'number' to 'i64'
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\json-parse-large\goose\main.goose
    +  output verified: 4/4 match oracle '613781'
```
<!-- test:json-parse-large:end -->

<!-- test:deep-recursion:start -->
### deep-recursion
_Updated 2026-11-13T12:53:19 by `python benchmarks/runner/run_benchmark.py`._

```text
  * deep-recursion  expected = 292249866 | 5,000-deep recursion, 200 rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        14.24    13.16   16.41  132.0  2.42x faster      3.7      123
  go          16.79    16.08   18.11  233.7  2.05x faster      5.8     1750
  cpp         16.90    15.51   19.76   91.9  2.04x faster      3.6       19
  cp3         34.41    17.59   110.6  109.7      baseline      5.5      414
  js          76.12    68.14   79.70   10.2  2.21x slower     38.3        -
  java        76.21    73.02   83.27    5.2  2.21x slower     39.1        1
  python      160.3    153.9   163.7    3.7  4.66x slower     11.7        -
  goose           -        -       -      -             -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\deep-recursion\goose\main.goose
    +  output verified: 7/7 match oracle '292249866'

gate: BIG WARN (fix this): deep-recursion: cp3 34 ms is 2.04x cpp (17 ms), tripwire 1.20x
gate: BIG WARN (fix this): deep-recursion: cp3 34 ms is 2.42x rust (14 ms), tripwire 1.20x
gate: BIG WARN (fix this): deep-recursion: cp3 34 ms is 2.05x go (17 ms), tripwire 1.00x
```
<!-- test:deep-recursion:end -->

<!-- test:exception-cost:start -->
### exception-cost
_Updated 2026-11-13T12:54:15 by `python benchmarks/runner/run_benchmark.py`._

```text
  * exception-cost  expected = 446742259 446742259 | 1,000,000 throw/catch vs sentinel-return iterations | median/best/p95 ms; ..
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  java        266.7    248.4   289.3   6.3       -    187.5        1
  python      399.9    392.0   433.7   5.2       -     11.0        -
  cpp         608.9    585.0  1779.0  75.8       -      4.8       21
  js         1667.0   1489.8  2894.2  52.4       -     46.7        -
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:21:1: error: CP3509: TypeScript allows throwing any value, but CP3 only allows Error
           objects (Error or a subclass). CP3 rejects throwing 'string' because a catch binds one static Error handle: there is no
           runtime metadata or hidden boxing in the native output. Construct an Error (or a subclass) and throw that instead.
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\exception-cost\goose\main.goose
    +  output verified: 4/4 match oracle '446742259 446742259'
```
<!-- test:exception-cost:end -->

<!-- test:file-big:start -->
### file-big
_Updated 2026-11-13T12:54:54 by `python benchmarks/runner/run_benchmark.py`._

```text
  * file-big  expected = 779913 | 8 MiB file, 8 write/read rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  js          207.4    186.7   219.8    6.0       -    117.9        -
  java        266.6    230.3   283.1    6.0       -    128.4        2
  rust        327.5    287.2   378.7  105.3       -     20.2      146
  cpp         366.0    300.4   375.2    9.5       -     21.1       31
  go          367.3    325.0   395.0   26.6       -     47.5     1891
  python      585.8    540.2   613.2    5.0       -     37.7        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:43:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-array\file-big\goose\main.goose
    +  output verified: 6/6 match oracle '779913'
```
<!-- test:file-big:end -->

<!-- test:tree-walk:start -->
### tree-walk
_Updated 2026-11-13T12:55:07 by `python benchmarks/runner/run_benchmark.py`._

```text
  * tree-walk  expected = 16383
66134180 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  go          14.22    11.09   28.32  239.0  2.02x faster      6.1     1752
  rust        20.06    12.58   31.44  167.2  1.43x faster      4.7      841
  cpp         20.12    15.28   28.03   62.3  1.43x faster      4.6       97
  cp3         28.68    22.42   164.7  146.3      baseline      7.2      419
  python      68.31    64.01   85.86   12.7  2.38x slower     12.4        -
  js          82.16    66.74   90.19   10.3  2.86x slower     50.6        -
  java        95.17    72.04   111.7   11.6  3.32x slower     39.6        1
  goose           -        -       -      -             -        -        -
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-object\tree-walk\goose\main.goose:2: error: cannot find
             the module rng, looked for
    +  output verified: 7/7 match oracle '16383\n66134180'

gate: BIG WARN (fix this): tree-walk: cp3 29 ms is 1.43x cpp (20 ms), tripwire 1.20x
gate: BIG WARN (fix this): tree-walk: cp3 29 ms is 1.43x rust (20 ms), tripwire 1.20x
gate: BIG WARN (fix this): tree-walk: cp3 29 ms is 2.02x go (14 ms), tripwire 1.00x
```
<!-- test:tree-walk:end -->

<!-- test:graph-bfs:start -->
### graph-bfs
_Updated 2026-11-13T13:13:45 by `python benchmarks/runner/run_benchmark.py`._

```text
  * graph-bfs  cross-check only (no oracle yet) | graph-bfs | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  go          35.40    24.95   52.50  170.6       -     13.2     1751
  rust        38.94    34.33   49.54   98.8       -     10.9      841
  cpp         44.47    41.22   51.32   64.7       -     10.1       47
  java        204.6    162.4   219.7   22.3       -     73.7        1
  python      902.1    676.3  1642.0   35.0       -     32.9        -
  js              -        -       -      -       -        -        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    x js  run failed: [runner] command timed out after 900s and was killed
    ! cp3  not runnable yet: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-map\graph-bfs\cp3\.cp3\build\src/index.cpp:274:13:
           error: no matching function for call to 'arrayWriteAt'
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-map\graph-bfs\goose\main.goose:2: error: cannot find
             the module rng, looked for
    +  output verified: 5/5 match across languages
```
<!-- test:graph-bfs:end -->

<!-- test:strlist:start -->
### strlist
_Updated 2026-11-13T13:14:09 by `python benchmarks/runner/run_benchmark.py`._

```text
  * strlist  expected = 100000
92177148 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         27.36    24.27   44.84   68.8       -      9.2       47
  rust        30.22    27.99   45.85   86.5       -     10.0      841
  go          35.90    19.86   44.62  154.8       -     14.8     1751
  java        140.1    114.5   153.7   10.2       -     53.5        1
  js          337.8    299.4   385.8   20.1       -     90.8        -
  python      848.0    802.3   874.4    2.5       -     18.2        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:8:1: error: CP3301: cannot find name 'f64'
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-string\strlist\goose\main.goose:2: error: cannot find
             the module rng, looked for
    +  output verified: 6/6 match oracle '100000\n92177148'
```
<!-- test:strlist:end -->

<!-- test:interp:start -->
### interp
_Updated 2026-11-13T13:14:23 by `python benchmarks/runner/run_benchmark.py`._

```text
  * interp  expected = 24
914237 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cpp         16.54    12.13   21.63   94.9  1.69x faster      4.6       48
  cp3         28.02    20.45   137.7  110.1      baseline      5.8      422
  python      45.29    41.40   66.88   18.8  1.62x slower     11.1        -
  java        84.95    79.97   110.3   14.1  3.03x slower     38.9        1
  js              -        -       -      -             -        -        -
  goose           -        -       -      -             -        -        -
    x js  run failed: file:///C:/Users/Krishnesh/dev/cp3/benchmarks/cases/dsa-object/interp/js/main.mjs:3
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-object\interp\goose\main.goose:2: error: cannot find
             the module rng, looked for
    +  output verified: 4/4 match oracle '24\n914237'

gate: BIG WARN (fix this): interp: cp3 28 ms is 1.69x cpp (17 ms), tripwire 1.20x
```
<!-- test:interp:end -->

<!-- test:blur:start -->
### blur
_Updated 2026-11-13T13:14:38 by `python benchmarks/runner/run_benchmark.py`._

```text
  * blur  expected = 256
7734030 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  rust        11.69    10.22   33.31  128.7       -      4.6      843
  go          19.25    12.93   44.49  163.6       -      5.8     1750
  cpp         31.72    21.92   89.66   68.4       -      5.1       49
  js          96.20    78.14   114.3   24.7       -     47.3        -
  java        130.9    105.5   157.6   13.6       -     38.9        1
  python      506.3    495.4   531.6    2.9       -     12.0        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:10:1: error: CP3420: the 'void' type is removed; annotate the return as 'undefined'
           instead
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-array\blur\goose\main.goose:2: error: cannot find the
             module rng, looked for
    +  output verified: 6/6 match oracle '256\n7734030'
```
<!-- test:blur:end -->

<!-- test:calc:start -->
### calc
_Updated 2026-11-13T13:14:53 by `python benchmarks/runner/run_benchmark.py`._

```text
  * calc  expected = 3750
1250
580458 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  goose       14.81    12.35   20.28  141.8  1.08x faster      3.3       58
  cp3         16.04    11.99   90.81  206.9      baseline      3.8      432
  go          18.32    11.77   76.06  208.8  1.14x slower      5.9     1754
  rust        18.66    12.56   24.13  144.3  1.16x slower      4.6      843
  cpp         21.11    13.40   28.22   86.8  1.32x slower      4.6       54
  js          90.69    79.31   115.1   13.0  5.65x slower     50.7        -
  python      100.2    81.72   143.0   22.8  6.25x slower     11.2        -
  java        124.5    95.56   136.5   11.0  7.76x slower     42.2        2
    +  output verified: 8/8 match oracle '3750\n1250\n580458'
```
<!-- test:calc:end -->

<!-- test:particles:start -->
### particles
_Updated 2026-11-13T13:15:13 by `python benchmarks/runner/run_benchmark.py`._

```text
  * particles  expected = 5000
28315536 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  goose       15.69    12.66   23.74  111.0   1.48x faster      3.9       58
  go          19.58    13.24   21.76  213.9   1.18x faster      5.8     1751
  rust        21.57    10.95   27.93  168.9   1.08x faster      4.6      840
  cpp         22.81    13.10   30.90   98.4   1.02x faster      5.0       45
  cp3         23.20    18.90   166.0  148.3       baseline      5.9      425
  js          90.29    74.20   121.1   16.6   3.89x slower     47.6        -
  java        113.4    93.95   128.0   13.4   4.89x slower     38.5        1
  python      462.3    435.2   494.0    5.0  19.93x slower     11.8        -
    +  output verified: 8/8 match oracle '5000\n28315536'

gate: WARN particles: cp3 23 ms is 1.08x rust (22 ms), watch 1.05x
gate: BIG WARN (fix this): particles: cp3 23 ms is 1.18x go (20 ms), tripwire 1.00x
```
<!-- test:particles:end -->

<!-- test:push:start -->
### push
_Updated 2026-11-13T13:15:41 by `python benchmarks/runner/run_benchmark.py`._

```text
  * push  expected = 1000000
15625
499758894813
750050 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  cpp         23.81    18.53   81.86   70.9   1.03x faster     12.2       46
  cp3         24.52    20.08   115.7  164.2       baseline     20.4      426
  go          29.97    23.76   49.66  128.4   1.22x slower     22.6     1752
  rust        34.40    22.06   58.29   79.2   1.40x slower     21.2      840
  java        109.7    105.1   126.4   10.8   4.47x slower     82.3        1
  js          400.1    370.1   419.6    5.3  16.32x slower    307.4        -
  python      736.6    712.3   760.5    2.3  30.04x slower    113.5        -
  goose           -        -       -      -              -        -        -
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-map\push\goose\main.goose:2: error: cannot find the
             module rng, looked for
    +  output verified: 7/7 match oracle '1000000\n15625\n499758894813\n750050'
```
<!-- test:push:end -->

<!-- test:records:start -->
### records
_Updated 2026-11-13T13:15:58 by `python benchmarks/runner/run_benchmark.py`._

```text
  * records  expected = 100000
17463411840 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  go          18.50    15.73   61.38  190.0  3.13x faster     11.2     1752
  goose       23.45    20.97   31.85   86.1  2.47x faster      7.8       58
  cpp         24.89    20.23   33.91   80.5  2.32x faster      9.2       47
  rust        28.93    18.37   35.71  135.3  2.00x faster     12.1      841
  cp3         57.87    49.68   209.7   96.6      baseline     16.4      426
  java        99.52    90.03   133.0   15.9  1.72x slower     45.2        2
  js          128.0    117.6   153.8   13.1  2.21x slower     76.3        -
  python      238.7    224.9   274.8    7.3  4.12x slower     26.2        -
    +  output verified: 8/8 match oracle '100000\n17463411840'

gate: BIG WARN (fix this): records: cp3 58 ms is 2.32x cpp (25 ms), tripwire 1.20x
gate: BIG WARN (fix this): records: cp3 58 ms is 2.00x rust (29 ms), tripwire 1.20x
gate: BIG WARN (fix this): records: cp3 58 ms is 3.13x go (18 ms), tripwire 1.00x
```
<!-- test:records:end -->

<!-- test:scene:start -->
### scene
_Updated 2026-11-13T13:16:05 by `python benchmarks/runner/run_benchmark.py`._

```text
  * scene  expected = 23
4747 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  rust        12.87    10.72   20.30  190.3       -      4.7      842
  goose       17.16    14.23   40.55   85.2       -      4.2       58
  cpp         17.83    12.73   21.22  109.4       -      4.6       50
  go          22.27    13.36   33.01  192.6       -      5.8     1752
  python      44.41    36.86   59.97   18.9       -     11.0        -
  java        79.06    69.21   97.50   18.4       -     38.9        2
  js          100.0    60.85   141.8   34.4       -     45.3        -
  cp3             -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:16:1: error: CP3420: the 'void' type is removed; annotate the return as 'undefined'
           instead
    +  output verified: 7/7 match oracle '23\n4747'
```
<!-- test:scene:end -->

<!-- test:sexp:start -->
### sexp
_Updated 2026-11-13T13:16:16 by `python benchmarks/runner/run_benchmark.py`._

```text
  * sexp  cross-check only (no oracle yet) | sexp | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  go          19.52    12.76   27.29  214.0       -      6.0     1754
  rust        20.32    15.88   31.56  107.0       -      5.1      841
  cpp         20.68    16.01   25.36   99.2       -      4.6       56
  js          79.63    71.30   114.4   16.9       -     51.0        -
  java        87.06    71.42   96.88   16.3       -     40.1        1
  python      100.7    93.41   107.6    5.9       -     11.6        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:11:1: error: CP3301: cannot find name 'f64'
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-string\sexp\goose\main.goose:2: error: cannot find the
             module rng, looked for
    x  rust output '122590\n6764934848165057546'; expected '56938\n13287444230854067034'
```
<!-- test:sexp:end -->

<!-- test:respond:start -->
### respond
_Updated 2026-11-13T13:16:50 by `python benchmarks/runner/run_benchmark.py`._

```text
  * respond  expected = 578079
3348416444655930007 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  goose       16.60    11.51   19.66  103.5   5.35x faster      4.7       58
  go          16.61    14.82   19.08  179.4   5.34x faster     10.0     1756
  cpp         17.27    13.87   29.78   65.9   5.14x faster      5.9       50
  rust        20.44    14.39   31.58  128.5   4.34x faster      5.8      842
  cp3         88.79    85.31   266.5   80.5       baseline      6.9      432
  java        110.0    92.80   127.8   15.9   1.24x slower     43.9        2
  js          128.9    117.4   145.8    8.2   1.45x slower     66.2        -
  python     1028.7    963.8  2036.8   37.3  11.59x slower     13.4        -
    +  output verified: 8/8 match oracle '578079\n3348416444655930007'

gate: BIG WARN (fix this): respond: cp3 89 ms is 5.14x cpp (17 ms), tripwire 1.20x
gate: BIG WARN (fix this): respond: cp3 89 ms is 4.34x rust (20 ms), tripwire 1.20x
gate: BIG WARN (fix this): respond: cp3 89 ms is 5.34x go (17 ms), tripwire 1.00x
```
<!-- test:respond:end -->

<!-- test:sum-array:start -->
### sum-array
_Updated 2026-11-13T13:17:56 by `python benchmarks/runner/run_benchmark.py`._

```text
  * sum-array  expected = 4000000
15987891712 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  cpp         34.80    32.36   44.00   70.9   1.60x faster     19.8       45
  rust        36.16    28.52   53.14  115.4   1.54x faster     19.8      841
  go          45.71    39.55   90.41  123.4   1.22x faster     22.3     1750
  cp3         55.79    50.27   209.0   90.1       baseline     38.4      418
  java        114.3    109.5   145.2   10.0   2.05x slower     54.8        1
  js          618.7    594.7   645.9    3.0  11.09x slower     62.5        -
  python     2994.9   2951.0  3179.4    2.4  53.69x slower    159.3        -
  goose           -        -       -      -              -        -        -
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-array\sum-array\goose\main.goose:2: error: cannot find
             the module rng, looked for
    +  output verified: 7/7 match oracle '4000000\n15987891712'

gate: BIG WARN (fix this): sum-array: cp3 56 ms is 1.60x cpp (35 ms), tripwire 1.20x
gate: BIG WARN (fix this): sum-array: cp3 56 ms is 1.54x rust (36 ms), tripwire 1.20x
gate: BIG WARN (fix this): sum-array: cp3 56 ms is 1.22x go (46 ms), tripwire 1.00x
```
<!-- test:sum-array:end -->
<!-- latest-by-test:end -->

## Latest full run

<!-- latest-full-run:start -->
_Generated by `python benchmarks/runner/run_suite.py` on 2026-11-13T13:17:56._

```text
BENCHMARK SUITE  2026-11-13T12:12:55 | 15 runs/test
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
  lang        ok/run  boot ms  p50 ms  p95 ms   req/s  RPS CV%  errors         vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------------------------------------------
  java           1/1   1146.0    0.30    0.55   9,823        -       0   1.23x faster    171.6        4
  cp3            1/1    545.6    0.29    0.45   7,955        -       0       baseline     17.7     7494
  go             1/1    537.2    0.13    0.47   7,585        -       0   1.05x slower     70.0    15630
  rust           1/1    520.8    0.50    0.82   7,122        -       0   1.12x slower      8.9     2154
  cpp            1/1    101.3    0.17    0.65   7,013        -       0   1.13x slower      6.3       60
  dotnet         1/1    588.7    0.21    0.64   6,852        -       0   1.16x slower    212.8        8
  js-fastify     1/1   1563.0    0.16    0.40   5,280        -       0   1.51x slower     69.6        -
  js             1/1   2572.2    0.27    0.57   3,001        -       0   2.65x slower     65.4        -
  python         1/1   2087.6    0.60    1.04     502        -       0  15.85x slower      4.1        -
  goose          0/1        -       -       -  no run        -       0              -        -        -
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
  lang        ok/run  boot ms  p50 ms  p95 ms   req/s  RPS CV%  errors         vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------------------------------------------
  cp3            1/1    517.3    0.21    0.47  64,797        -       0       baseline     22.9     7498
  java           1/1   1121.6    0.35    0.61  60,988        -       0   1.06x slower    362.4        4
  cpp            1/1    520.8    0.11    0.54  48,085        -      12   1.35x slower      6.4       61
  js-fastify     1/1   2078.6    0.37    0.56  37,118        -       0   1.75x slower     54.4        -
  go             1/1    517.6    0.34    0.58  35,556        -       0   1.82x slower     72.5    15630
  dotnet         1/1    546.9    0.35    0.63  25,361        -       0   2.55x slower    350.1        9
  js             1/1   1567.3    0.26    0.55  21,288        -       0   3.04x slower     50.3        -
  rust           1/1    522.9    0.50    0.70  19,225        -      28   3.37x slower     10.0     2154
  python         1/1   3145.4    1.00    1.63   5,947        -       0  10.90x slower      4.0        -
  goose          0/1        -       -       -  no run        -       0              -        -        -
  > compute-single: cpp x15 ...
  > compute-single: rust x15 ...
  > compute-single: go x15 ...
  > compute-single: js x15 ...
  > compute-single: python x15 ...
  > compute-single: java x15 ...
  > compute-single: cp3 x15 ...
  > compute-single: goose x15 ...

  * compute-single  expected = 131434424 (total Collatz steps for 1..1,000,000) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms   p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        205.2    193.2    260.6  30.8   2.36x faster      4.5      122
  cpp         262.7    213.8    321.6  33.7   1.84x faster      5.1       19
  go          324.0    288.6    501.4  32.0   1.49x faster      6.1     1750
  java        405.6    388.6    426.6   4.3   1.19x faster     39.2        1
  cp3         483.3    477.0    642.0  16.7       baseline      5.7      414
  js         1605.1   1581.2   1676.3   2.4   3.32x slower     38.3        -
  python    22707.3  20630.6  23571.9   9.2  46.98x slower     10.9        -
  goose           -        -        -     -              -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\compute-single\goose\main.goose
    +  output verified: 7/7 match oracle '131434424'
  > compute-single-int: cpp x15 ...
  > compute-single-int: rust x15 ...
  > compute-single-int: go x15 ...
  > compute-single-int: js x15 ...
  > compute-single-int: python x15 ...
  > compute-single-int: java x15 ...
  > compute-single-int: cp3 x15 ...
  > compute-single-int: goose x15 ...

  * compute-single-int  expected = 131434424 (total Collatz steps for 1..1,000,000) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms   p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        196.4    189.2    212.2  29.5   2.33x faster      4.1      123
  cpp         225.0    212.0    232.1  13.2   2.03x faster      4.6       19
  go          292.3    286.0    295.3  30.1   1.57x faster      5.9     1750
  java        371.3    360.6    378.6   1.5   1.23x faster     38.9        1
  cp3         457.8    451.8    594.1  19.1       baseline      5.7      414
  js         1590.9   1565.6   1610.7   1.0   3.47x slower     45.4        -
  python    28326.3  26193.4  28890.8   2.8  61.87x slower     10.9        -
  goose           -        -        -     -              -        -        -
    x goose  build failed: cannot open file:
             C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\compute-single-int\goose\main.goose
    +  output verified: 7/7 match oracle '131434424'
  > compute-multi: cpp x15 ...
  > compute-multi: rust x15 ...
  > compute-multi: go x15 ...
  > compute-multi: js x15 ...
  > compute-multi: python x15 ...
  > compute-multi: java x15 ...
  > compute-multi: cp3 x15 ...
  > compute-multi: goose x15 ...

  * compute-multi  expected = 131434424 (same total split across all cores) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  rust        37.75    26.02   46.92   87.5       -      4.6      154
  cpp         43.09    32.58   65.83   59.5       -      5.4       21
  go          54.72    45.23   71.67  130.8       -      7.1     1759
  java        136.7    123.2   171.9   12.2       -     41.4        1
  js          325.9    298.2   347.8   10.8       -    283.7        -
  python     1533.9   1463.0  1640.0    4.2       -     19.7        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:34:1: error: CP3401: type mismatch: cannot assign 'number' to 'i64'
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\compute-multi\goose\main.goose
    +  output verified: 6/6 match oracle '131434424'
  > hello-world: cpp x15 ...
  > hello-world: rust x15 ...
  > hello-world: go x15 ...
  > hello-world: js x15 ...
  > hello-world: python x15 ...
  > hello-world: java x15 ...
  > hello-world: cp3 x15 ...
  > hello-world: goose x15 ...

  * hello-world  expected = Hello, world! | spawn -> exit wall time; 15 runs | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cpp         16.25    11.33   29.17   99.8  1.56x faster      4.6       19
  go          17.10    11.77   26.19  231.8  1.48x faster      5.7     1750
  rust        17.30    11.63   24.97  121.9  1.47x faster      4.1      122
  cp3         25.39    18.25   260.1  162.8      baseline      5.2      410
  python      36.88    32.95   44.73   22.3  1.45x slower     10.8        -
  js          63.52    57.34   67.33    5.9  2.50x slower     35.2        -
  java        70.36    63.28   80.65   10.5  2.77x slower     38.8        0
  goose           -        -       -      -             -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\hello-world\goose\main.goose
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
  python      41.02    35.82   60.40   22.6  73.71x faster        -        -
  js          70.19    58.90   96.50   16.8  43.07x faster        -        -
  java        549.4    523.7   597.8    4.5   5.50x faster        -        -
  cpp         722.4    705.1   799.2   98.4   4.18x faster        -        -
  rust        746.5    683.5   864.5   33.7   4.05x faster        -        -
  go         1928.5   1664.3  2167.6  193.2   1.57x faster        -        -
  cp3        3023.2   2917.6  3211.2    4.1       baseline        -        -
    +  output verified: 7/7 match oracle 'Hello, world!'
  > string-churn: cpp x15 ...
  > string-churn: rust x15 ...
  > string-churn: go x15 ...
  > string-churn: js x15 ...
  > string-churn: python x15 ...
  > string-churn: java x15 ...
  > string-churn: cp3 x15 ...
  > string-churn: goose x15 ...

  * string-churn  expected = 852167 | scale: default (50000 iterations) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cp3         324.1    318.4   481.0  23.4       baseline      5.5      428
  rust        353.1    348.2   359.4  36.6   1.09x slower      4.5      166
  python     2028.8   1978.6  2071.8   1.5   6.26x slower     11.0        -
  cpp        2207.3   2135.9  2278.4   2.5   6.81x slower      5.0       23
  js         2657.6   2588.1  2743.4   2.1   8.20x slower     46.4        -
  java       2727.7   2627.6  2760.5   1.3   8.42x slower    653.5        2
  go         5787.3   4941.0  6255.3  17.0  17.86x slower     98.3     1765
  goose           -        -       -     -              -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-string\string-churn\goose\main.goose
    +  output verified: 7/7 match oracle '852167'
  - omit  sha256-churn: cpp: no SHA-256 in the C++ standard library; vendoring a hash implementation compares dependencies
  - omit  sha256-churn: rust: no SHA-256 in Rust std without a crate; adding sha2 compares dependencies
  > sha256-churn: go x15 ...
  > sha256-churn: js x15 ...
  > sha256-churn: python x15 ...
  > sha256-churn: java x15 ...
  > sha256-churn: cp3 x15 ...
  > sha256-churn: goose x15 ...

  * sha256-churn  expected = 218229 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cp3         28.70    20.37   124.2  158.7      baseline      6.2      674
  go          62.40    56.83   71.63  117.8  2.17x slower      6.1     1922
  python      136.0    133.8   141.3    5.5  4.74x slower     13.3        -
  js          147.5    139.5   156.7    4.2  5.14x slower     41.9        -
  java        171.1    160.7   181.1    3.5  5.96x slower     49.6        1
  goose           -        -       -      -             -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\sha256-churn\goose\main.goose
    +  output verified: 5/5 match oracle '218229'
  > buffer-copy: cpp x15 ...
  > buffer-copy: rust x15 ...
  > buffer-copy: go x15 ...
  > buffer-copy: js x15 ...
  > buffer-copy: python x15 ...
  > buffer-copy: java x15 ...
  > buffer-copy: cp3 x15 ...
  > buffer-copy: goose x15 ...

  * buffer-copy  expected = 688024 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        29.53    24.49   38.26  183.8  1.11x faster      4.3      125
  cp3         32.67    26.57   183.2  136.5      baseline      5.9      426
  python      71.30    64.42   97.49   16.9  2.18x slower     11.7        -
  go          74.05    62.64   81.14  103.4  2.27x slower     76.2     1752
  cpp         106.6    23.15   135.3   63.3  3.26x slower      4.8       21
  java        173.2    161.8   237.2   15.2  5.30x slower    264.2        1
  js          255.6    235.9   263.8    4.5  7.82x slower    116.1        -
  goose           -        -       -      -             -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-array\buffer-copy\goose\main.goose
    +  output verified: 7/7 match oracle '688024'
  > typed-array-sum: cpp x15 ...
  > typed-array-sum: rust x15 ...
  > typed-array-sum: go x15 ...
  > typed-array-sum: js x15 ...
  > typed-array-sum: python x15 ...
  > typed-array-sum: java x15 ...
  > typed-array-sum: cp3 x15 ...
  > typed-array-sum: goose x15 ...

  * typed-array-sum  expected = 135618465 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  rust        35.77    33.51   43.68  233.1       -     12.7      128
  cpp         37.62    31.89   54.00   59.4       -     13.2       22
  go          48.30    38.09   62.06  135.9       -     15.7     1750
  js          100.7    94.47   135.1   14.0       -     49.2        -
  java        127.8    122.8   132.9    2.8       -     48.4        1
  python     1530.7   1502.7  1546.9    1.2       -     20.1        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:51:1: error: CP3401: type mismatch: cannot assign 'number' to 'i64'
    x goose  build failed: cannot open file:
             C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-array\typed-array-sum\goose\main.goose
    +  output verified: 6/6 match oracle '135618465'
  > math-kernel: cpp x15 ...
  > math-kernel: rust x15 ...
  > math-kernel: go x15 ...
  > math-kernel: js x15 ...
  > math-kernel: python x15 ...
  > math-kernel: java x15 ...
  > math-kernel: cp3 x15 ...
  > math-kernel: goose x15 ...

  * math-kernel  expected = 177047762 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  rust        42.75    38.41   45.89  191.4       -      4.1      124
  cpp         117.6    114.5   121.3   19.6       -      4.6       20
  go          125.0    117.8   145.9   69.7       -      6.1     1762
  js          158.7    152.3   190.5    9.0       -     40.1        -
  java        175.2    165.4   194.5    4.5       -     39.3        1
  python     3074.2   2967.3  3486.3    6.4       -     11.0        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:36:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\math-kernel\goose\main.goose
    +  output verified: 6/6 match oracle '177047762'
  > word-frequency: cpp x15 ...
  > word-frequency: rust x15 ...
  > word-frequency: go x15 ...
  > word-frequency: js x15 ...
  > word-frequency: python x15 ...
  > word-frequency: java x15 ...
  > word-frequency: cp3 x15 ...
  > word-frequency: goose x15 ...

  * word-frequency  expected = 37425343 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  go          54.67    45.15   65.75  123.5  1.99x faster      6.0     1762
  rust        80.64    67.47   115.6   39.1  1.35x faster      4.4      130
  cpp         85.03    67.70   127.1   29.7  1.28x faster      5.2       23
  cp3         109.1    104.4   264.9   69.4      baseline      6.2      711
  java        163.6    156.3   167.4    2.2  1.50x slower     72.5        1
  js          249.3    241.7   261.0    3.7  2.29x slower     38.9        -
  python      693.5    672.8   709.6    1.4  6.36x slower     11.0        -
  goose           -        -       -      -             -        -        -
    x goose  build failed: cannot open file:
             C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-string\word-frequency\goose\main.goose
    +  output verified: 7/7 match oracle '37425343'
  > object-churn: cpp x15 ...
  > object-churn: rust x15 ...
  > object-churn: go x15 ...
  > object-churn: js x15 ...
  > object-churn: python x15 ...
  > object-churn: java x15 ...
  > object-churn: cp3 x15 ...
  > object-churn: goose x15 ...

  * object-churn  expected = 90641269 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  go          75.43    71.25   80.33  95.2       -     46.6     1751
  cpp         157.1    151.8   183.4  13.5       -     62.4       20
  rust        161.7    156.9   174.7  58.8       -     61.9      124
  java        189.3    178.9   222.2   7.4       -    132.6        1
  js          450.9    431.9   507.2   5.0       -    307.1        -
  python     1978.2   1925.3  2110.4  10.0       -    233.9        -
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet:
           C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-object\object-churn\cp3\.cp3\build\src/index.cpp:265:17: error: no
           matching function for call to 'arrayPush'
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-object\object-churn\goose\main.goose
    +  output verified: 6/6 match oracle '90641269'
  > array-growth: cpp x15 ...
  > array-growth: rust x15 ...
  > array-growth: go x15 ...
  > array-growth: js x15 ...
  > array-growth: python x15 ...
  > array-growth: java x15 ...
  > array-growth: cp3 x15 ...
  > array-growth: goose x15 ...

  * array-growth  expected = 200000010000000 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cp3         64.30    59.15   211.4  86.8       baseline    157.9      414
  rust        147.7    134.2   178.6  19.0   2.30x slower    261.1      124
  cpp         155.2    140.5   176.5  14.9   2.41x slower    261.4       20
  java        295.9    271.9   305.7   3.7   4.60x slower    554.9        1
  go          306.2    273.6   340.7  29.2   4.76x slower    625.1     1750
  js          423.9    403.3   452.6   5.2   6.59x slower    363.3        -
  python     1975.2   1927.1  2028.2   1.7  30.72x slower    832.0        -
  goose           -        -       -     -              -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-array\array-growth\goose\main.goose
    +  output verified: 7/7 match oracle '200000010000000'
  > closure-capture: cpp x15 ...
  > closure-capture: rust x15 ...
  > closure-capture: go x15 ...
  > closure-capture: js x15 ...
  > closure-capture: python x15 ...
  > closure-capture: java x15 ...
  > closure-capture: cp3 x15 ...
  > closure-capture: goose x15 ...

  * closure-capture  expected = 16666688333331 998521500000 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  rust        24.45    20.11   28.51  187.6   2.44x faster      4.1      125
  go          26.91    20.68   32.97  195.9   2.21x faster      6.0     1752
  cp3         59.53    52.76   189.0   92.9       baseline     81.7      420
  cpp         106.2    89.40   111.6   25.4   1.78x slower    182.8       21
  java        157.2    139.2   173.1    5.5   2.64x slower     78.6        2
  js          852.4    817.0   928.3    4.5  14.32x slower    293.1        -
  python     1512.2   1469.1  1573.9    2.0  25.40x slower     11.3        -
  goose           -        -       -      -              -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\closure-capture\goose\main.goose
    +  output verified: 7/7 match oracle '16666688333331 998521500000'
  > interface-dispatch: cpp x15 ...
  > interface-dispatch: rust x15 ...
  > interface-dispatch: go x15 ...
  > interface-dispatch: js x15 ...
  > interface-dispatch: python x15 ...
  > interface-dispatch: java x15 ...
  > interface-dispatch: cp3 x15 ...
  > interface-dispatch: goose x15 ...

  * interface-dispatch  expected = 37999986 66000000 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  go          59.67    49.66   64.95  119.3       -     63.6     1754
  rust        134.6    131.0   139.2  101.5       -     90.6      126
  cpp         158.3    151.8   163.8   17.5       -    111.5       22
  java        216.3    178.7   248.7   11.5       -    155.1        2
  js          580.5    544.2   596.5    3.0       -    388.7        -
  python     1821.1   1639.5  1974.0    6.9       -    237.2        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:79:1: error: CP3401: type mismatch: cannot assign 'number' to 'i64'
    x goose  build failed: cannot open file:
             C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\interface-dispatch\goose\main.goose
    +  output verified: 6/6 match oracle '37999986 66000000'
  > generics-mono: cpp x15 ...
  > generics-mono: rust x15 ...
  > generics-mono: go x15 ...
  > generics-mono: js x15 ...
  > generics-mono: python x15 ...
  > generics-mono: java x15 ...
  > generics-mono: cp3 x15 ...
  > generics-mono: goose x15 ...

  * generics-mono  expected = 998986007 998986007 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         32.34    26.26   44.93   77.4       -      4.6       19
  rust        33.26    28.98   56.64   89.5       -      4.1      124
  go          35.37    31.70   125.7  127.3       -      6.1     1750
  js          104.1    102.6   132.6   13.9       -     40.4        -
  java        137.6    122.5   173.6   13.6       -     51.9        2
  python      651.4    628.9   673.3    3.0       -     11.0        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:49:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\generics-mono\goose\main.goose
    +  output verified: 6/6 match oracle '998986007 998986007'
  - omit  json-roundtrip: cpp: no JSON in the C++ standard library; a dependency-free file would vendor a parser
  - omit  json-roundtrip: rust: no JSON in Rust std without a crate; adding serde_json compares dependencies
  > json-roundtrip: go x15 ...
  > json-roundtrip: js x15 ...
  > json-roundtrip: python x15 ...
  > json-roundtrip: java x15 ...
  > json-roundtrip: cp3 x15 ...
  > json-roundtrip: goose x15 ...

  * json-roundtrip  expected = 664346 | 20,000 parse+stringify round trips of one 310-byte record | median/best/p95 ms; CV = va..
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  js          130.3    121.4   145.1   5.2       -     40.6        -
  go          164.2    157.4   200.0  54.0       -     12.8     2154
  java        264.3    257.8   279.0   2.7       -    155.6        4
  python      320.2    309.2   331.5   2.6       -     12.6        -
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:79:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\json-roundtrip\goose\main.goose
    +  output verified: 4/4 match oracle '664346'
  - omit  async-fanout: cpp: no timers or event loop in the C++ standard library; a file would hand-roll both
  - omit  async-fanout: rust: no timers or executor in Rust std; adding tokio/async-std compares dependencies
  > async-fanout: go x15 ...
  > async-fanout: js x15 ...
  > async-fanout: python x15 ...
  > async-fanout: java x15 ...
  > async-fanout: cp3 x15 ...
  > async-fanout: goose x15 ...

  * async-fanout  expected = 99898 | 10,000-timer fan-out joined at once, then 10,000 chained awaits | median/best/p95 ms; CV =..
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  go          23.03    21.62   25.22  195.3   1.02x faster     11.9     1760
  cp3         23.61    18.04   209.4  167.4       baseline      9.6      814
  js          97.60    87.34   106.8    9.1   4.13x slower     51.1        -
  java        220.0    201.1   251.1    6.9   9.32x slower     99.0        3
  python      262.3    252.3   341.2   11.1  11.11x slower     35.0        -
  goose           -        -       -      -              -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\async-fanout\goose\main.goose
    +  output verified: 5/5 match oracle '99898'
  > env-injection: cpp x15 ...
  > env-injection: rust x15 ...
  > env-injection: go x15 ...
  > env-injection: js x15 ...
  > env-injection: python x15 ...
  > env-injection: java x15 ...
  > env-injection: cp3 x15 ...
  > env-injection: goose x15 ...

  * env-injection  expected = 133644 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  java        131.1    126.3   150.4   6.2       -     62.1        1
  rust        144.8    141.5   264.4  26.4       -      4.1      126
  cpp         170.9    161.8   216.6  33.5       -      4.6       21
  go          185.8    173.2   256.5  42.9       -     12.2     1755
  js          348.5    342.0   355.4   2.2       -     40.3        -
  python      391.8    383.5   399.0   1.6       -     11.0        -
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:37:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\env-injection\goose\main.goose
    +  output verified: 6/6 match oracle '133644'
  > file-roundtrip: cpp x15 ...
  > file-roundtrip: rust x15 ...
  > file-roundtrip: go x15 ...
  > file-roundtrip: js x15 ...
  > file-roundtrip: python x15 ...
  > file-roundtrip: java x15 ...
  > file-roundtrip: cp3 x15 ...
  > file-roundtrip: goose x15 ...

  * file-roundtrip  expected = 698140 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  java        258.9    249.0   323.3  10.6       -     43.9        2
  rust        340.9    318.1   396.7  11.7       -      4.2      148
  cpp         372.4    340.5   517.5  14.9       -      5.2       33
  go          386.2    357.6   517.2  28.0       -      8.1     1900
  js          416.6    366.2   455.8   7.1       -     40.7        -
  python      467.5    431.2   835.5  37.6       -     13.8        -
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:53:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\file-roundtrip\goose\main.goose
    +  output verified: 6/6 match oracle '698140'
  > path-join: cpp x15 ...
  > path-join: rust x15 ...
  > path-join: go x15 ...
  > path-join: js x15 ...
  > path-join: python x15 ...
  > path-join: java x15 ...
  > path-join: cp3 x15 ...
  > path-join: goose x15 ...

  * path-join  expected = 425577 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  go          79.68    74.70   84.29  86.2       -     13.7     1768
  rust        203.5    197.2   217.9  79.4       -      4.2      135
  java        331.6    319.3   360.6   4.2       -    150.6        1
  js          462.6    437.6   509.3   7.0       -     42.2        -
  cpp         563.8    552.6   630.8   5.1       -      5.4       31
  python      833.8    722.1   932.0   6.2       -     10.9        -
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:30:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\path-join\goose\main.goose
    +  output verified: 6/6 match oracle '425577'
  > date-format: cpp x15 ...
  - omit  date-format: rust: no timezone-aware date formatting in Rust std without a crate; adding chrono compares dependencies
  > date-format: go x15 ...
  > date-format: js x15 ...
  > date-format: python x15 ...
  > date-format: java x15 ...
  > date-format: cp3 x15 ...
  > date-format: goose x15 ...

  * date-format  expected = 796897 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         37.18    33.59   42.46   74.4       -      4.7       47
  go          37.35    31.81   49.11  163.5       -      7.5     1815
  js          96.89    90.44   103.2    5.8       -     40.6        -
  java        111.1    108.3   141.0   11.8       -     55.5        2
  python      200.7    194.9   206.5    4.0       -     11.1        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:79:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\date-format\goose\main.goose
    +  output verified: 5/5 match oracle '796897'
  > os-probe: cpp x15 ...
  - omit  os-probe: rust: no hostname/platform probe in Rust std without libc or a crate; hand-rolling it compares bindings
  > os-probe: go x15 ...
  > os-probe: js x15 ...
  > os-probe: python x15 ...
  > os-probe: java x15 ...
  > os-probe: cp3 x15 ...
  > os-probe: goose x15 ...

  * os-probe  expected = 208981 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  java        204.7    194.4   228.8   6.3       -     43.2        2
  cpp         239.2    233.1   263.3  10.8       -      4.7       21
  go          506.5    498.0   860.9  36.8       -     12.2     1758
  python      635.5    628.4   657.7   1.6       -     14.1        -
  js         2048.5   1935.0  2183.1   8.2       -     42.1        -
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:43:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\os-probe\goose\main.goose
    +  output verified: 5/5 match oracle '208981'
  > binary-trees: cpp x15 ...
  > binary-trees: rust x15 ...
  > binary-trees: go x15 ...
  > binary-trees: js x15 ...
  > binary-trees: python x15 ...
  > binary-trees: java x15 ...
  > binary-trees: cp3 x15 ...
  > binary-trees: goose x15 ...

  * binary-trees  expected = 1348958 | full binary trees to depth 13, recursive build and check | median/best/p95 ms; CV = vari..
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        48.60    40.73   59.50  134.8  7.27x faster      4.6      124
  cpp         70.73    67.82   233.3   71.3  4.99x faster      5.7       34
  go          74.44    48.61  2095.7  198.9  4.74x faster     13.3     1751
  java        87.08    80.26   92.00    5.1  4.06x faster     60.4        1
  js          96.23    80.61   109.1    9.0  3.67x faster     52.3        -
  python      280.5    277.2   295.8    3.5  1.26x faster     12.5        -
  cp3         353.2    347.1   489.3   23.3      baseline      9.0      420
  goose           -        -       -      -             -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-object\binary-trees\goose\main.goose
    +  output verified: 7/7 match oracle '1348958'
  > map-churn: cpp x15 ...
  > map-churn: rust x15 ...
  > map-churn: go x15 ...
  > map-churn: js x15 ...
  > map-churn: python x15 ...
  > map-churn: java x15 ...
  > map-churn: cp3 x15 ...
  > map-churn: goose x15 ...

  * map-churn  expected = 942471769 | 100,000-key hash map, delete/reinsert/update churn | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         22.03    20.40   31.94   79.0       -      9.0       21
  rust        22.62    21.25   32.05   85.8       -      6.4      128
  go          24.65    23.75   26.51  196.4       -      8.2     1756
  python      69.79    68.04   75.17    7.2       -     23.8        -
  js          87.48    79.06   103.2   11.1       -     47.0        -
  java        109.8    107.6   141.3   10.2       -     70.9        1
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:39:1: error: CP3401: type mismatch: cannot assign 'number' to 'i64'
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-map\map-churn\goose\main.goose
    +  output verified: 6/6 match oracle '942471769'
  > utf8-text: cpp x15 ...
  > utf8-text: rust x15 ...
  > utf8-text: go x15 ...
  > utf8-text: js x15 ...
  > utf8-text: python x15 ...
  > utf8-text: java x15 ...
  > utf8-text: cp3 x15 ...
  > utf8-text: goose x15 ...

  * utf8-text  expected = 78156718 | 417,792-byte UTF-8 text, 32 manual decode rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         50.25    44.73   81.89   41.3       -      5.1       21
  go          52.93    49.34   59.97  124.5       -      6.4     1756
  rust        54.15    49.20   55.50  129.1       -      4.5      124
  java        125.3    118.1   133.1    4.6       -     42.2        2
  js          196.9    193.0   217.6    4.3       -     40.8        -
  python     2188.5   2174.1  2263.4    1.4       -     12.4        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:62:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-string\utf8-text\goose\main.goose
    +  output verified: 6/6 match oracle '78156718'
  > regex-scan: cpp x15 ...
  - omit  regex-scan: rust: no regex engine in Rust std; adding the regex crate compares dependencies
  > regex-scan: go x15 ...
  > regex-scan: js x15 ...
  > regex-scan: python x15 ...
  > regex-scan: java x15 ...
  > regex-scan: cp3 x15 ...
  > regex-scan: goose x15 ...

  * regex-scan  expected = 735703090 | 240,000-byte text, global [0-9]+ scan, 8 rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  js          84.75    75.73   90.16   5.9       -     42.4        -
  go          99.29    93.07   103.0  77.3       -     12.1     2074
  python      99.37    94.63   122.8  10.1       -     15.3        -
  cpp         127.3    122.9   133.9  33.7       -      5.1      130
  java        140.2    123.8   143.7   8.4       -     56.2        1
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:41:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-string\regex-scan\goose\main.goose
    +  output verified: 5/5 match oracle '735703090'
  - omit  json-parse-large: cpp: no JSON in the C++ standard library; a dependency-free file would vendor a parser
  - omit  json-parse-large: rust: no JSON in Rust std without a crate; adding serde_json compares dependencies
  > json-parse-large: go x15 ...
  > json-parse-large: js x15 ...
  > json-parse-large: python x15 ...
  > json-parse-large: java x15 ...
  > json-parse-large: cp3 x15 ...
  > json-parse-large: goose x15 ...

  * json-parse-large  expected = 613781 | 204,472-byte JSON record, 20 parse rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  js          92.86    86.94   99.41   3.9       -     52.0        -
  python      127.7    125.0   132.7   5.9       -     16.6        -
  go          129.0    123.2   144.6  61.2       -     16.8     2154
  java        190.6    183.7   213.4   6.5       -    132.3        3
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:79:1: error: CP3401: type mismatch: cannot assign 'number' to 'i64'
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\stdlib\json-parse-large\goose\main.goose
    +  output verified: 4/4 match oracle '613781'
  > deep-recursion: cpp x15 ...
  > deep-recursion: rust x15 ...
  > deep-recursion: go x15 ...
  > deep-recursion: js x15 ...
  > deep-recursion: python x15 ...
  > deep-recursion: java x15 ...
  > deep-recursion: cp3 x15 ...
  > deep-recursion: goose x15 ...

  * deep-recursion  expected = 292249866 | 5,000-deep recursion, 200 rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        14.24    13.16   16.41  132.0  2.42x faster      3.7      123
  go          16.79    16.08   18.11  233.7  2.05x faster      5.8     1750
  cpp         16.90    15.51   19.76   91.9  2.04x faster      3.6       19
  cp3         34.41    17.59   110.6  109.7      baseline      5.5      414
  js          76.12    68.14   79.70   10.2  2.21x slower     38.3        -
  java        76.21    73.02   83.27    5.2  2.21x slower     39.1        1
  python      160.3    153.9   163.7    3.7  4.66x slower     11.7        -
  goose           -        -       -      -             -        -        -
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\deep-recursion\goose\main.goose
    +  output verified: 7/7 match oracle '292249866'
  > exception-cost: cpp x15 ...
  - omit  exception-cost: rust: no exceptions in Rust; Result is the returned-value path, not throw/catch
  - omit  exception-cost: go: no exceptions in Go; error values are the returned-value path, not throw/catch
  > exception-cost: js x15 ...
  > exception-cost: python x15 ...
  > exception-cost: java x15 ...
  > exception-cost: cp3 x15 ...
  > exception-cost: goose x15 ...

  * exception-cost  expected = 446742259 446742259 | 1,000,000 throw/catch vs sentinel-return iterations | median/best/p95 ms; ..
  lang    median ms  best ms  p95 ms   CV%  vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------
  java        266.7    248.4   289.3   6.3       -    187.5        1
  python      399.9    392.0   433.7   5.2       -     11.0        -
  cpp         608.9    585.0  1779.0  75.8       -      4.8       21
  js         1667.0   1489.8  2894.2  52.4       -     46.7        -
  cp3             -        -       -     -       -        -        -
  goose           -        -       -     -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:21:1: error: CP3509: TypeScript allows throwing any value, but CP3 only allows Error
           objects (Error or a subclass). CP3 rejects throwing 'string' because a catch binds one static Error handle: there is no
           runtime metadata or hidden boxing in the native output. Construct an Error (or a subclass) and throw that instead.
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\compute\exception-cost\goose\main.goose
    +  output verified: 4/4 match oracle '446742259 446742259'
  > file-big: cpp x15 ...
  > file-big: rust x15 ...
  > file-big: go x15 ...
  > file-big: js x15 ...
  > file-big: python x15 ...
  > file-big: java x15 ...
  > file-big: cp3 x15 ...
  > file-big: goose x15 ...

  * file-big  expected = 779913 | 8 MiB file, 8 write/read rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  js          207.4    186.7   219.8    6.0       -    117.9        -
  java        266.6    230.3   283.1    6.0       -    128.4        2
  rust        327.5    287.2   378.7  105.3       -     20.2      146
  cpp         366.0    300.4   375.2    9.5       -     21.1       31
  go          367.3    325.0   395.0   26.6       -     47.5     1891
  python      585.8    540.2   613.2    5.0       -     37.7        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:43:1: error: CP3406: the native operator '%' requires integral operands; convert the
           number side explicitly
    x goose  build failed: cannot open file: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-array\file-big\goose\main.goose
    +  output verified: 6/6 match oracle '779913'
  > tree-walk: cpp x15 ...
  > tree-walk: rust x15 ...
  > tree-walk: go x15 ...
  > tree-walk: js x15 ...
  > tree-walk: python x15 ...
  > tree-walk: java x15 ...
  > tree-walk: cp3 x15 ...
  > tree-walk: goose x15 ...

  * tree-walk  expected = 16383
66134180 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  go          14.22    11.09   28.32  239.0  2.02x faster      6.1     1752
  rust        20.06    12.58   31.44  167.2  1.43x faster      4.7      841
  cpp         20.12    15.28   28.03   62.3  1.43x faster      4.6       97
  cp3         28.68    22.42   164.7  146.3      baseline      7.2      419
  python      68.31    64.01   85.86   12.7  2.38x slower     12.4        -
  js          82.16    66.74   90.19   10.3  2.86x slower     50.6        -
  java        95.17    72.04   111.7   11.6  3.32x slower     39.6        1
  goose           -        -       -      -             -        -        -
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-object\tree-walk\goose\main.goose:2: error: cannot find
             the module rng, looked for
    +  output verified: 7/7 match oracle '16383\n66134180'
  > graph-bfs: cpp x15 ...
  > graph-bfs: rust x15 ...
  > graph-bfs: go x15 ...
  > graph-bfs: js x15 ...
  > graph-bfs: python x15 ...
  > graph-bfs: java x15 ...
  > graph-bfs: cp3 x15 ...
  > graph-bfs: goose x15 ...

  * graph-bfs  cross-check only (no oracle yet) | graph-bfs | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  go          35.40    24.95   52.50  170.6       -     13.2     1751
  rust        38.94    34.33   49.54   98.8       -     10.9      841
  cpp         44.47    41.22   51.32   64.7       -     10.1       47
  java        204.6    162.4   219.7   22.3       -     73.7        1
  python      902.1    676.3  1642.0   35.0       -     32.9        -
  js              -        -       -      -       -        -        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    x js  run failed: [runner] command timed out after 900s and was killed
    ! cp3  not runnable yet: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-map\graph-bfs\cp3\.cp3\build\src/index.cpp:274:13:
           error: no matching function for call to 'arrayWriteAt'
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-map\graph-bfs\goose\main.goose:2: error: cannot find
             the module rng, looked for
    +  output verified: 5/5 match across languages
  > strlist: cpp x15 ...
  > strlist: rust x15 ...
  > strlist: go x15 ...
  > strlist: js x15 ...
  > strlist: python x15 ...
  > strlist: java x15 ...
  > strlist: cp3 x15 ...
  > strlist: goose x15 ...

  * strlist  expected = 100000
92177148 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  cpp         27.36    24.27   44.84   68.8       -      9.2       47
  rust        30.22    27.99   45.85   86.5       -     10.0      841
  go          35.90    19.86   44.62  154.8       -     14.8     1751
  java        140.1    114.5   153.7   10.2       -     53.5        1
  js          337.8    299.4   385.8   20.1       -     90.8        -
  python      848.0    802.3   874.4    2.5       -     18.2        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:8:1: error: CP3301: cannot find name 'f64'
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-string\strlist\goose\main.goose:2: error: cannot find
             the module rng, looked for
    +  output verified: 6/6 match oracle '100000\n92177148'
  - lru: pending (no cases folder yet) -- skipped
  > interp: cpp x15 ...
  - omit  interp: rust: no virtual dispatch in Rust; enum + match is the idiomatic analogue (kept for cross-language comparison
          but listed here to match exception-cost)
  - omit  interp: go: no inheritance in Go; interface + type-switch is the idiomatic analogue
  > interp: js x15 ...
  > interp: python x15 ...
  > interp: java x15 ...
  > interp: cp3 x15 ...
  > interp: goose x15 ...

  * interp  expected = 24
914237 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cpp         16.54    12.13   21.63   94.9  1.69x faster      4.6       48
  cp3         28.02    20.45   137.7  110.1      baseline      5.8      422
  python      45.29    41.40   66.88   18.8  1.62x slower     11.1        -
  java        84.95    79.97   110.3   14.1  3.03x slower     38.9        1
  js              -        -       -      -             -        -        -
  goose           -        -       -      -             -        -        -
    x js  run failed: file:///C:/Users/Krishnesh/dev/cp3/benchmarks/cases/dsa-object/interp/js/main.mjs:3
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-object\interp\goose\main.goose:2: error: cannot find
             the module rng, looked for
    +  output verified: 4/4 match oracle '24\n914237'
  > blur: cpp x15 ...
  > blur: rust x15 ...
  > blur: go x15 ...
  > blur: js x15 ...
  > blur: python x15 ...
  > blur: java x15 ...
  > blur: cp3 x15 ...
  > blur: goose x15 ...

  * blur  expected = 256
7734030 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  rust        11.69    10.22   33.31  128.7       -      4.6      843
  go          19.25    12.93   44.49  163.6       -      5.8     1750
  cpp         31.72    21.92   89.66   68.4       -      5.1       49
  js          96.20    78.14   114.3   24.7       -     47.3        -
  java        130.9    105.5   157.6   13.6       -     38.9        1
  python      506.3    495.4   531.6    2.9       -     12.0        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:10:1: error: CP3420: the 'void' type is removed; annotate the return as 'undefined'
           instead
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-array\blur\goose\main.goose:2: error: cannot find the
             module rng, looked for
    +  output verified: 6/6 match oracle '256\n7734030'
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
  goose       14.81    12.35   20.28  141.8  1.08x faster      3.3       58
  cp3         16.04    11.99   90.81  206.9      baseline      3.8      432
  go          18.32    11.77   76.06  208.8  1.14x slower      5.9     1754
  rust        18.66    12.56   24.13  144.3  1.16x slower      4.6      843
  cpp         21.11    13.40   28.22   86.8  1.32x slower      4.6       54
  js          90.69    79.31   115.1   13.0  5.65x slower     50.7        -
  python      100.2    81.72   143.0   22.8  6.25x slower     11.2        -
  java        124.5    95.56   136.5   11.0  7.76x slower     42.2        2
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
  goose       15.69    12.66   23.74  111.0   1.48x faster      3.9       58
  go          19.58    13.24   21.76  213.9   1.18x faster      5.8     1751
  rust        21.57    10.95   27.93  168.9   1.08x faster      4.6      840
  cpp         22.81    13.10   30.90   98.4   1.02x faster      5.0       45
  cp3         23.20    18.90   166.0  148.3       baseline      5.9      425
  js          90.29    74.20   121.1   16.6   3.89x slower     47.6        -
  java        113.4    93.95   128.0   13.4   4.89x slower     38.5        1
  python      462.3    435.2   494.0    5.0  19.93x slower     11.8        -
    +  output verified: 8/8 match oracle '5000\n28315536'
  > push: cpp x15 ...
  > push: rust x15 ...
  > push: go x15 ...
  > push: js x15 ...
  > push: python x15 ...
  > push: java x15 ...
  > push: cp3 x15 ...
  > push: goose x15 ...

  * push  expected = 1000000
15625
499758894813
750050 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  cpp         23.81    18.53   81.86   70.9   1.03x faster     12.2       46
  cp3         24.52    20.08   115.7  164.2       baseline     20.4      426
  go          29.97    23.76   49.66  128.4   1.22x slower     22.6     1752
  rust        34.40    22.06   58.29   79.2   1.40x slower     21.2      840
  java        109.7    105.1   126.4   10.8   4.47x slower     82.3        1
  js          400.1    370.1   419.6    5.3  16.32x slower    307.4        -
  python      736.6    712.3   760.5    2.3  30.04x slower    113.5        -
  goose           -        -       -      -              -        -        -
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-map\push\goose\main.goose:2: error: cannot find the
             module rng, looked for
    +  output verified: 7/7 match oracle '1000000\n15625\n499758894813\n750050'
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
  go          18.50    15.73   61.38  190.0  3.13x faster     11.2     1752
  goose       23.45    20.97   31.85   86.1  2.47x faster      7.8       58
  cpp         24.89    20.23   33.91   80.5  2.32x faster      9.2       47
  rust        28.93    18.37   35.71  135.3  2.00x faster     12.1      841
  cp3         57.87    49.68   209.7   96.6      baseline     16.4      426
  java        99.52    90.03   133.0   15.9  1.72x slower     45.2        2
  js          128.0    117.6   153.8   13.1  2.21x slower     76.3        -
  python      238.7    224.9   274.8    7.3  4.12x slower     26.2        -
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
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  rust        12.87    10.72   20.30  190.3       -      4.7      842
  goose       17.16    14.23   40.55   85.2       -      4.2       58
  cpp         17.83    12.73   21.22  109.4       -      4.6       50
  go          22.27    13.36   33.01  192.6       -      5.8     1752
  python      44.41    36.86   59.97   18.9       -     11.0        -
  java        79.06    69.21   97.50   18.4       -     38.9        2
  js          100.0    60.85   141.8   34.4       -     45.3        -
  cp3             -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:16:1: error: CP3420: the 'void' type is removed; annotate the return as 'undefined'
           instead
    +  output verified: 7/7 match oracle '23\n4747'
  > sexp: cpp x15 ...
  > sexp: rust x15 ...
  > sexp: go x15 ...
  > sexp: js x15 ...
  > sexp: python x15 ...
  > sexp: java x15 ...
  > sexp: cp3 x15 ...
  > sexp: goose x15 ...

  * sexp  cross-check only (no oracle yet) | sexp | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%  vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------
  go          19.52    12.76   27.29  214.0       -      6.0     1754
  rust        20.32    15.88   31.56  107.0       -      5.1      841
  cpp         20.68    16.01   25.36   99.2       -      4.6       56
  js          79.63    71.30   114.4   16.9       -     51.0        -
  java        87.06    71.42   96.88   16.3       -     40.1        1
  python      100.7    93.41   107.6    5.9       -     11.6        -
  cp3             -        -       -      -       -        -        -
  goose           -        -       -      -       -        -        -
    ! cp3  not runnable yet: src/index.cp3:11:1: error: CP3301: cannot find name 'f64'
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-string\sexp\goose\main.goose:2: error: cannot find the
             module rng, looked for
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
  goose       16.60    11.51   19.66  103.5   5.35x faster      4.7       58
  go          16.61    14.82   19.08  179.4   5.34x faster     10.0     1756
  cpp         17.27    13.87   29.78   65.9   5.14x faster      5.9       50
  rust        20.44    14.39   31.58  128.5   4.34x faster      5.8      842
  cp3         88.79    85.31   266.5   80.5       baseline      6.9      432
  java        110.0    92.80   127.8   15.9   1.24x slower     43.9        2
  js          128.9    117.4   145.8    8.2   1.45x slower     66.2        -
  python     1028.7    963.8  2036.8   37.3  11.59x slower     13.4        -
    +  output verified: 8/8 match oracle '578079\n3348416444655930007'
  > sum-array: cpp x15 ...
  > sum-array: rust x15 ...
  > sum-array: go x15 ...
  > sum-array: js x15 ...
  > sum-array: python x15 ...
  > sum-array: java x15 ...
  > sum-array: cp3 x15 ...
  > sum-array: goose x15 ...

  * sum-array  expected = 4000000
15987891712 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  --------------------------------------------------------------------------
  cpp         34.80    32.36   44.00   70.9   1.60x faster     19.8       45
  rust        36.16    28.52   53.14  115.4   1.54x faster     19.8      841
  go          45.71    39.55   90.41  123.4   1.22x faster     22.3     1750
  cp3         55.79    50.27   209.0   90.1       baseline     38.4      418
  java        114.3    109.5   145.2   10.0   2.05x slower     54.8        1
  js          618.7    594.7   645.9    3.0  11.09x slower     62.5        -
  python     2994.9   2951.0  3179.4    2.4  53.69x slower    159.3        -
  goose           -        -       -      -              -        -        -
    x goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-array\sum-array\goose\main.goose:2: error: cannot find
             the module rng, looked for
    +  output verified: 7/7 match oracle '4000000\n15987891712'

  * performance gate  D64 target: cp3 within about 5% of cpp/rust (WARN over 1.05x); BIG WARN over 1.20x cpp/rust or slower tha..
  ------------------------------------------------------------------------------------------------------------
  ! BIG WARN  health-server: cp3 7955 req/s is 1.23x slower than java (9823 req/s), tripwire 1.00x
  ! BIG WARN  compute-single: cp3 483 ms is 1.84x cpp (263 ms), tripwire 1.20x
  ! BIG WARN  compute-single: cp3 483 ms is 2.36x rust (205 ms), tripwire 1.20x
  ! BIG WARN  compute-single: cp3 483 ms is 1.49x go (324 ms), tripwire 1.00x
  ! BIG WARN  compute-single: cp3 483 ms is 1.19x java (406 ms), tripwire 1.00x
  ! BIG WARN  compute-single-int: cp3 458 ms is 2.03x cpp (225 ms), tripwire 1.20x
  ! BIG WARN  compute-single-int: cp3 458 ms is 2.33x rust (196 ms), tripwire 1.20x
  ! BIG WARN  compute-single-int: cp3 458 ms is 1.57x go (292 ms), tripwire 1.00x
  ! BIG WARN  compute-single-int: cp3 458 ms is 1.23x java (371 ms), tripwire 1.00x
  ! BIG WARN  hello-world: cp3 25 ms is 1.56x cpp (16 ms), tripwire 1.20x
  ! BIG WARN  hello-world: cp3 25 ms is 1.47x rust (17 ms), tripwire 1.20x
  ! BIG WARN  hello-world: cp3 25 ms is 1.48x go (17 ms), tripwire 1.00x
  ! BIG WARN  dev-reload: cp3 3023 ms is 4.18x cpp (722 ms), tripwire 1.20x
  ! BIG WARN  dev-reload: cp3 3023 ms is 4.05x rust (747 ms), tripwire 1.20x
  ! BIG WARN  dev-reload: cp3 3023 ms is 1.57x go (1929 ms), tripwire 1.00x
  ! BIG WARN  dev-reload: cp3 3023 ms is 43.07x js (70 ms), tripwire 1.00x
  ! BIG WARN  dev-reload: cp3 3023 ms is 73.71x python (41 ms), tripwire 1.00x
  ! BIG WARN  dev-reload: cp3 3023 ms is 5.50x java (549 ms), tripwire 1.00x
  ! WARN      buffer-copy: cp3 33 ms is 1.11x rust (30 ms), watch 1.05x
  ! BIG WARN  word-frequency: cp3 109 ms is 1.28x cpp (85 ms), tripwire 1.20x
  ! BIG WARN  word-frequency: cp3 109 ms is 1.35x rust (81 ms), tripwire 1.20x
  ! BIG WARN  word-frequency: cp3 109 ms is 1.99x go (55 ms), tripwire 1.00x
  ! BIG WARN  closure-capture: cp3 60 ms is 2.44x rust (24 ms), tripwire 1.20x
  ! BIG WARN  closure-capture: cp3 60 ms is 2.21x go (27 ms), tripwire 1.00x
  ! BIG WARN  async-fanout: cp3 24 ms is 1.02x go (23 ms), tripwire 1.00x
  ! BIG WARN  binary-trees: cp3 353 ms is 4.99x cpp (71 ms), tripwire 1.20x
  ! BIG WARN  binary-trees: cp3 353 ms is 7.27x rust (49 ms), tripwire 1.20x
  ! BIG WARN  binary-trees: cp3 353 ms is 4.74x go (74 ms), tripwire 1.00x
  ! BIG WARN  binary-trees: cp3 353 ms is 3.67x js (96 ms), tripwire 1.00x
  ! BIG WARN  binary-trees: cp3 353 ms is 1.26x python (281 ms), tripwire 1.00x
  ! BIG WARN  binary-trees: cp3 353 ms is 4.06x java (87 ms), tripwire 1.00x
  ! BIG WARN  deep-recursion: cp3 34 ms is 2.04x cpp (17 ms), tripwire 1.20x
  ! BIG WARN  deep-recursion: cp3 34 ms is 2.42x rust (14 ms), tripwire 1.20x
  ! BIG WARN  deep-recursion: cp3 34 ms is 2.05x go (17 ms), tripwire 1.00x
  ! BIG WARN  tree-walk: cp3 29 ms is 1.43x cpp (20 ms), tripwire 1.20x
  ! BIG WARN  tree-walk: cp3 29 ms is 1.43x rust (20 ms), tripwire 1.20x
  ! BIG WARN  tree-walk: cp3 29 ms is 2.02x go (14 ms), tripwire 1.00x
  ! BIG WARN  interp: cp3 28 ms is 1.69x cpp (17 ms), tripwire 1.20x
  ! WARN      particles: cp3 23 ms is 1.08x rust (22 ms), watch 1.05x
  ! BIG WARN  particles: cp3 23 ms is 1.18x go (20 ms), tripwire 1.00x
  ! BIG WARN  records: cp3 58 ms is 2.32x cpp (25 ms), tripwire 1.20x
  ! BIG WARN  records: cp3 58 ms is 2.00x rust (29 ms), tripwire 1.20x
  ! BIG WARN  records: cp3 58 ms is 3.13x go (18 ms), tripwire 1.00x
  ! BIG WARN  respond: cp3 89 ms is 5.14x cpp (17 ms), tripwire 1.20x
  ! BIG WARN  respond: cp3 89 ms is 4.34x rust (20 ms), tripwire 1.20x
  ! BIG WARN  respond: cp3 89 ms is 5.34x go (17 ms), tripwire 1.00x
  ! BIG WARN  sum-array: cp3 56 ms is 1.60x cpp (35 ms), tripwire 1.20x
  ! BIG WARN  sum-array: cp3 56 ms is 1.54x rust (36 ms), tripwire 1.20x
  ! BIG WARN  sum-array: cp3 56 ms is 1.22x go (46 ms), tripwire 1.00x
  ------------------------------------------------------------------------------------------------------------
  ! gate: 47 BIG WARN(s), 2 WARN(s); rerun with --strict to fail on warnings; 65 row(s) did not run
```
<!-- latest-full-run:end -->
