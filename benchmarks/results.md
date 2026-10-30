

## Latest results by test

<!-- latest-by-test:start -->
<!-- test:health-server:start -->
### health-server
_Updated 2026-10-30T16:55:14 by `python benchmarks/runner/run_benchmark.py`._

```text
  ◆ health-server  median boot/req/s; pooled latency p50/p95; CV = run-to-run variation %
  lang        ok/run  boot ms  p50 ms  p95 ms   req/s  RPS CV%  errors        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────────────────────────────────
  cp3            1/1    536.8    0.36    0.50  15,719        -       0      baseline      9.5     1616
  js-fastify     1/1    555.7    0.19    0.56  15,054        -       0  1.04x slower     68.1        -
  java           1/1   1191.1    0.34    0.75  14,863        -       0  1.06x slower    269.2        4
  cpp            1/1    528.4    0.22    0.34  14,858        -       0  1.06x slower     10.9       42
  go             1/1    509.4    0.17    0.29  14,066        -       0  1.12x slower     21.2    15630
  rust           1/1    538.2    0.13    0.26  14,016        -       0  1.12x slower      6.9     1098
  js             1/1    568.9    0.36    0.53  13,980        -       0  1.12x slower     62.4        -
  python         1/1   1644.7    0.89    1.18   1,816        -       0  8.66x slower      4.1        -
```
<!-- test:health-server:end -->

<!-- test:health-server-multi:start -->
### health-server-multi
_Updated 2026-10-30T16:57:25 by `python benchmarks/runner/run_benchmark.py`._

```text
  ◆ health-server-multi  median boot/req/s; pooled latency p50/p95; CV = run-to-run variation %
  lang        ok/run  boot ms  p50 ms  p95 ms   req/s  RPS CV%  errors        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────────────────────────────────
  cp3            1/1    520.6    0.31    0.43  81,815        -       0      baseline     21.1     1620
  cpp            1/1    523.5    0.23    0.35  81,344        -       0  1.01x slower     14.0       42
  rust           1/1    546.6    0.17    0.46  73,709        -       0  1.11x slower      8.4     1098
  js-fastify     1/1   1032.6    0.27    0.45  70,671        -       0  1.16x slower     54.3        -
  java           1/1   1134.4    0.37    0.75  70,038        -       0  1.17x slower    346.8        4
  go             1/1    521.4    0.13    0.32  65,450        -       0  1.25x slower     23.6    15630
  js             1/1   1076.1    0.46    0.75  58,165        -       0  1.41x slower     50.7        -
  python         1/1   3166.0    1.00    1.65  12,474        -       0  6.56x slower      4.1        -
```
<!-- test:health-server-multi:end -->

<!-- test:compute-single:start -->
### compute-single
_Updated 2026-10-30T17:02:13 by `python benchmarks/runner/run_benchmark.py`._

```text
  ◆ compute-single  expected = 131434424 (total Collatz steps for 1..1,000,000) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms   p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  ──────────────────────────────────────────────────────────────────────────
  cp3         151.8    126.4    259.8  27.1       baseline      3.6      434
  rust        224.9    224.0    229.5  28.6   1.48x slower      4.1      122
  cpp         251.4    248.9    267.3  11.4   1.66x slower      4.1       19
  go          359.8    345.2    371.1  27.4   2.37x slower      5.7     1750
  java        801.0    782.5    830.6   4.8   5.28x slower     39.0        1
  js         1895.5   1870.8   1938.9  19.1  12.48x slower     38.4        -
  python    11385.9  11280.6  11451.8   3.0  74.99x slower     11.0        -
    ✓  output verified: 7/7 match oracle '131434424'
```
<!-- test:compute-single:end -->

<!-- test:compute-multi:start -->
### compute-multi
_Updated 2026-10-30T17:06:54 by `python benchmarks/runner/run_benchmark.py`._

```text
  ◆ compute-multi  expected = 131434424 (same total split across all cores) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  ──────────────────────────────────────────────────────────────────────────
  cp3         25.59    24.94   206.6  149.5       baseline      4.9      463
  rust        29.87    26.52   38.46   94.4   1.17x slower      4.6      154
  cpp         40.71    36.12   45.73   80.0   1.59x slower      5.3       21
  go          58.41    56.05   65.38  112.3   2.28x slower      6.8     1759
  java        163.8    147.8   193.0    8.8   6.40x slower     41.7        1
  js          392.4    367.8   422.0    5.1  15.33x slower    286.2        -
  python     1736.3   1704.7  1842.8    2.9  67.85x slower     19.8        -
    ✓  output verified: 7/7 match oracle '131434424'
```
<!-- test:compute-multi:end -->

<!-- test:hello-world:start -->
### hello-world
_Updated 2026-10-30T17:07:03 by `python benchmarks/runner/run_benchmark.py`._

```text
  ◆ hello-world  expected = Hello, world! | spawn -> exit wall time; 15 runs | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  cp3          7.85     7.19   141.4  227.2      baseline      2.1      433
  rust         8.40     7.56    9.66  181.8  1.07x slower      4.1      122
  go           9.70     8.71   15.17  257.7  1.23x slower      5.7     1750
  cpp         13.59    10.38   20.44  128.8  1.73x slower      4.6       19
  python      40.08    36.32   41.60    6.8  5.10x slower     10.9        -
  js          66.94    62.29   72.43    5.7  8.52x slower     35.2        -
  java        74.99    72.06   78.17    3.5  9.55x slower     39.0        0
    ✓  output verified: 7/7 match oracle 'Hello, world!'
```
<!-- test:hello-world:end -->

<!-- test:string-churn:start -->
### string-churn
_Updated 2026-10-30T17:11:17 by `python benchmarks/runner/run_benchmark.py`._

```text
  ◆ string-churn  expected = 852167 | scale: default (50000 iterations) | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  cp3         394.8    379.9   524.9  12.3       baseline      3.8      450
  rust        427.0    413.9   451.6   3.8   1.08x slower      4.0      166
  python     2440.9   2368.6  2484.8   1.6   6.18x slower     11.2        -
  cpp        2744.2   2713.9  2776.8   0.9   6.95x slower      4.4       23
  js         3124.1   3087.7  3185.2   1.4   7.91x slower     46.3        -
  java       3326.1   3178.0  3384.9   1.8   8.43x slower    440.9        2
  go         4061.7   3939.1  4195.6   2.0  10.29x slower     70.3     1765
    ✓  output verified: 7/7 match oracle '852167'
```
<!-- test:string-churn:end -->

<!-- test:sha256-churn:start -->
### sha256-churn
_Updated 2026-10-30T17:11:33 by `python benchmarks/runner/run_benchmark.py`._

```text
  ◆ sha256-churn  expected = 218229 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  ──────────────────────────────────────────────────────────────────────────
  cp3         19.43    19.08   193.9  171.2       baseline      5.3      694
  go          66.98    66.14   72.89  115.4   3.45x slower      6.4     1922
  python      159.9    156.9   167.2    2.2   8.23x slower     13.4        -
  js          168.9    163.3   178.9    3.6   8.69x slower     42.0        -
  java        204.0    193.4   214.5    3.6  10.50x slower     51.0        1
    ✓  output verified: 5/5 match oracle '218229'
```
<!-- test:sha256-churn:end -->

<!-- test:buffer-copy:start -->
### buffer-copy
_Updated 2026-10-30T17:29:49 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ buffer-copy  expected = 688024 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  ──────────────────────────────────────────────────────────────────────────
  rust        19.15    18.84   28.67   16.9   1.43x faster      4.1      125
  cp3         27.43    26.89   192.2  103.3       baseline      3.8      450
  go          75.45    62.53   78.21    8.0   2.75x slower     72.9     1752
  python      83.71    74.45   116.3   19.1   3.05x slower     11.7        -
  cpp         121.1    21.85   152.8   69.2   4.42x slower      4.6       21
  java        181.7    177.4   195.9    3.4   6.63x slower    264.4        1
  js          276.0    264.6   333.9    8.4  10.07x slower    111.2        -
    ✓  output verified: 7/7 match oracle '688024'

gate: BIG WARN (fix this): buffer-copy: cp3 27 ms is 1.43x rust (19 ms), tripwire 1.20x
```
<!-- test:buffer-copy:end -->

<!-- test:typed-array-sum:start -->
### typed-array-sum
_Updated 2026-10-30T17:12:35 by `python benchmarks/runner/run_benchmark.py`._

```text
  ◆ typed-array-sum  expected = 135618465 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  ──────────────────────────────────────────────────────────────────────────
  cp3         19.44    18.14   159.2  171.0       baseline     12.8      462
  cpp         29.85    28.90   30.76   53.6   1.54x slower     13.3       22
  rust        40.37    37.97   47.96  193.8   2.08x slower     12.8      128
  go          43.74    42.18   49.66  127.9   2.25x slower     15.6     1750
  js          115.5    108.7   117.5    3.3   5.94x slower     49.3        -
  java        122.8    118.9   135.2    6.2   6.32x slower     48.4        1
  python     1804.9   1781.0  1845.4    1.2  92.85x slower     20.2        -
    ✓  output verified: 7/7 match oracle '135618465'
```
<!-- test:typed-array-sum:end -->

<!-- test:math-kernel:start -->
### math-kernel
_Updated 2026-10-30T17:46:51 by `python benchmarks/runner/run_benchmark.py --only math-kernel --reps 7`._

```text
  * math-kernel  expected = 177047762 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        42.74    41.76   76.96  27.3   1.77x faster      3.6      124
  cp3         75.58    74.97   260.9  68.5       baseline      3.6      442
  go          134.6    133.3   172.1  10.2   1.78x slower      5.8     1762
  cpp         146.7    139.8   185.6  10.1   1.94x slower      4.1       20
  js          182.8    178.5   192.8   2.5   2.42x slower     40.1        -
  java        199.7    194.4   202.2   1.7   2.64x slower     39.2        1
  python     3671.2   3648.0  3832.4   1.7  48.57x slower     11.2        -
    +  output verified: 7/7 match oracle '177047762'

gate: BIG WARN (fix this): math-kernel: cp3 76 ms is 1.77x rust (43 ms), tripwire 1.20x
```
<!-- test:math-kernel:end -->

<!-- test:word-frequency:start -->
### word-frequency
_Updated 2026-10-30T17:30:41 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ word-frequency  expected = 37425343 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  go          52.25    51.06   77.93  17.1   1.35x faster      5.8     1762
  cp3         70.61    68.09   236.9  69.4       baseline      3.7      455
  cpp         71.57    70.53   101.2  15.0   1.01x slower      4.6       23
  rust        79.20    76.93   95.54   8.9   1.12x slower      3.9      130
  java        171.7    162.9   202.1   7.4   2.43x slower     74.1        1
  js          291.0    272.8   308.5   4.1   4.12x slower     38.5        -
  python      808.3    783.4   823.4   1.7  11.45x slower     11.0        -
    ✓  output verified: 7/7 match oracle '37425343'

gate: BIG WARN (fix this): word-frequency: cp3 71 ms is 1.35x go (52 ms), tripwire 1.00x
```
<!-- test:word-frequency:end -->

<!-- test:object-churn:start -->
### object-churn
_Updated 2026-10-30T17:31:11 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ object-churn  expected = 90641269 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  cp3         54.26    50.88   224.9  81.8       baseline     22.8      438
  go          82.19    78.11   87.44   3.9   1.51x slower     46.1     1751
  cpp         175.8    170.4   186.3   3.5   3.24x slower     61.9       20
  rust        184.1    177.4   199.2   3.7   3.39x slower     61.4      124
  java        197.1    178.7   199.5   4.4   3.63x slower    130.7        1
  js          510.0    499.2   533.9   2.2   9.40x slower    303.7        -
  python     2123.5   2075.2  2159.5   1.5  39.14x slower    232.5        -
    ✓  output verified: 7/7 match oracle '90641269'
```
<!-- test:object-churn:end -->

<!-- test:array-growth:start -->
### array-growth
_Updated 2026-10-30T17:31:42 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ array-growth  expected = 200000010000000 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  cpp         141.7    135.1   246.8  25.5   1.22x faster    261.1       20
  rust        160.8    139.9   224.0  15.6   1.07x faster    261.2      124
  cp3         172.8    158.4   432.7  46.9       baseline    156.2      434
  go          272.6    247.7   504.1  29.2   1.58x slower    625.1     1750
  java        282.7    270.1   309.5   4.8   1.64x slower    554.6        1
  js          400.6    394.3   435.8   4.0   2.32x slower    353.2        -
  python     2231.7   2209.7  2254.5   0.7  12.91x slower    830.6        -
    ✓  output verified: 7/7 match oracle '200000010000000'

gate: BIG WARN (fix this): array-growth: cp3 173 ms is 1.22x cpp (142 ms), tripwire 1.20x
gate: WARN array-growth: cp3 173 ms is 1.07x rust (161 ms), watch 1.05x
```
<!-- test:array-growth:end -->

<!-- test:closure-capture:start -->
### closure-capture
_Updated 2026-10-30T17:32:11 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ closure-capture  expected = 16666688333331 998521500000 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  ──────────────────────────────────────────────────────────────────────────
  rust        22.14    21.19   179.2  128.9   2.63x faster      4.1      125
  go          24.52    22.15   358.4  172.3   2.37x faster      6.1     1752
  cp3         58.14    50.44   371.7  102.9       baseline     80.4      442
  cpp         97.18    94.87   159.2   22.0   1.67x slower    182.2       21
  java        174.2    159.0   210.0    9.3   3.00x slower     79.9        2
  js          970.5    939.3  1005.1    2.1  16.69x slower    315.8        -
  python     1786.3   1773.8  1797.7    0.5  30.73x slower     11.4        -
    ✓  output verified: 7/7 match oracle '16666688333331 998521500000'

gate: BIG WARN (fix this): closure-capture: cp3 58 ms is 2.63x rust (22 ms), tripwire 1.20x
gate: BIG WARN (fix this): closure-capture: cp3 58 ms is 2.37x go (25 ms), tripwire 1.00x
```
<!-- test:closure-capture:end -->

<!-- test:interface-dispatch:start -->
### interface-dispatch
_Updated 2026-10-30T17:32:44 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ interface-dispatch  expected = 37999986 66000000 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  go          56.96    55.64   459.3  133.1  6.57x faster     63.5     1754
  rust        161.2    156.8   457.4   55.2  2.32x faster     90.7      126
  cpp         196.4    171.3   261.4   14.9  1.91x faster    111.5       22
  java        231.2    222.4   282.7    8.8  1.62x faster    154.8        2
  cp3         374.2    356.1   590.8   21.1      baseline    213.7      452
  js          679.7    651.5   714.3    3.2  1.82x slower    388.7        -
  python     1975.6   1918.8  2079.2    2.7  5.28x slower    236.8        -
    ✓  output verified: 7/7 match oracle '37999986 66000000'

gate: BIG WARN (fix this): interface-dispatch: cp3 374 ms is 1.91x cpp (196 ms), tripwire 1.20x
gate: BIG WARN (fix this): interface-dispatch: cp3 374 ms is 2.32x rust (161 ms), tripwire 1.20x
gate: BIG WARN (fix this): interface-dispatch: cp3 374 ms is 6.57x go (57 ms), tripwire 1.00x
gate: BIG WARN (fix this): interface-dispatch: cp3 374 ms is 1.62x java (231 ms), tripwire 1.00x
```
<!-- test:interface-dispatch:end -->

<!-- test:generics-mono:start -->
### generics-mono
_Updated 2026-10-30T17:32:59 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ generics-mono  expected = 998986007 998986007 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  ──────────────────────────────────────────────────────────────────────────
  cpp         29.45    26.02   119.4   83.0   1.88x faster      4.6       19
  rust        31.68    29.98   273.5  137.8   1.75x faster      4.1      124
  go          32.26    30.79   255.0  131.7   1.72x faster      6.0     1750
  cp3         55.45    52.92   371.7  105.1       baseline      4.6      441
  js          102.2    96.78   110.7    4.8   1.84x slower     40.3        -
  java        139.5    128.8   155.6    6.9   2.52x slower     51.5        2
  python      769.8    750.8   821.8    3.0  13.88x slower     11.1        -
    ✓  output verified: 7/7 match oracle '998986007 998986007'

gate: BIG WARN (fix this): generics-mono: cp3 55 ms is 1.88x cpp (29 ms), tripwire 1.20x
gate: BIG WARN (fix this): generics-mono: cp3 55 ms is 1.75x rust (32 ms), tripwire 1.20x
gate: BIG WARN (fix this): generics-mono: cp3 55 ms is 1.72x go (32 ms), tripwire 1.00x
```
<!-- test:generics-mono:end -->

<!-- test:json-roundtrip:start -->
### json-roundtrip
_Updated 2026-10-30T17:33:14 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ json-roundtrip  expected = 664346 | 20,000 parse+stringify round trips of one 310-byte record | median/best/p95 ms; CV = var…
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  js          126.0    120.1   148.5   7.3  2.30x faster     40.6        -
  go          183.6    174.9   510.2  53.1  1.58x faster     13.7     2154
  java        284.7    281.4   303.0   2.6  1.02x faster    156.3        4
  cp3         289.7    278.5   589.7  33.2      baseline      4.4      492
  python      343.7    331.3   350.1   2.1  1.19x slower     12.6        -
    ✓  output verified: 5/5 match oracle '664346'

gate: BIG WARN (fix this): json-roundtrip: cp3 290 ms is 1.58x go (184 ms), tripwire 1.00x
gate: BIG WARN (fix this): json-roundtrip: cp3 290 ms is 2.30x js (126 ms), tripwire 1.00x
gate: BIG WARN (fix this): json-roundtrip: cp3 290 ms is 1.02x java (285 ms), tripwire 1.00x
```
<!-- test:json-roundtrip:end -->

<!-- test:async-fanout:start -->
### async-fanout
_Updated 2026-10-30T17:33:45 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ async-fanout  expected = 99898 | 10,000-timer fan-out joined at once, then 10,000 chained awaits | median/best/p95 ms; CV = …
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  ──────────────────────────────────────────────────────────────────────────
  cp3         17.50    16.97   264.4  140.9       baseline      7.9      926
  go          18.02    16.88   424.3  200.4   1.03x slower      8.8     1760
  js          89.63    86.87   94.32    2.8   5.12x slower     51.1        -
  java        235.3    199.4   297.5   12.2  13.45x slower     99.8        3
  python      299.1    289.7   353.4    7.5  17.09x slower     34.9        -
    ✓  output verified: 5/5 match oracle '99898'
```
<!-- test:async-fanout:end -->

<!-- test:env-injection:start -->
### env-injection
_Updated 2026-10-30T17:34:04 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ env-injection  expected = 133644 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  cp3         112.2    108.7   416.5  69.1      baseline      4.2      455
  java        133.0    128.6   156.9   6.9  1.19x slower     61.9        1
  rust        172.9    168.3   333.5  30.9  1.54x slower      4.2      126
  go          201.1    196.0   605.2  59.1  1.79x slower     12.1     1755
  cpp         230.0    221.7   359.6  19.8  2.05x slower      4.6       21
  js          417.5    410.7   427.0   1.5  3.72x slower     40.3        -
  python      466.8    453.5   471.3   1.5  4.16x slower     11.1        -
    ✓  output verified: 7/7 match oracle '133644'
```
<!-- test:env-injection:end -->

<!-- test:file-roundtrip:start -->
### file-roundtrip
_Updated 2026-10-30T17:34:40 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ file-roundtrip  expected = 698140 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  java        254.6    232.7   284.8   7.2  1.95x faster     44.1        2
  js          361.9    348.4   545.7  17.5  1.37x faster     40.8        -
  rust        475.8    343.6   632.1  24.8  1.04x faster      4.3      148
  go          482.4    323.9   997.1  40.7  1.03x faster      8.2     1900
  cp3         495.4    318.8   825.9  38.0      baseline      4.6     1352
  python      567.9    456.1   673.7  16.0  1.15x slower     13.9        -
  cpp         585.3    348.4   621.1  22.7  1.18x slower      5.5       33
    ✓  output verified: 7/7 match oracle '698140'

gate: BIG WARN (fix this): file-roundtrip: cp3 495 ms is 1.03x go (482 ms), tripwire 1.00x
gate: BIG WARN (fix this): file-roundtrip: cp3 495 ms is 1.37x js (362 ms), tripwire 1.00x
gate: BIG WARN (fix this): file-roundtrip: cp3 495 ms is 1.95x java (255 ms), tripwire 1.00x
```
<!-- test:file-roundtrip:end -->

<!-- test:path-join:start -->
### path-join
_Updated 2026-10-30T17:35:11 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ path-join  expected = 425577 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  go          85.20    83.80   474.1  104.2  4.13x faster     11.5     1768
  rust        246.8    233.1   825.5   64.3  1.43x faster      4.2      135
  cp3         352.2    335.8   578.4   22.8      baseline      4.6      449
  java        397.7    385.2   414.5    2.5  1.13x slower    152.2        1
  js          492.5    485.3   646.5   11.4  1.40x slower     42.3        -
  cpp         595.6    586.1  1065.6   27.0  1.69x slower      4.8       31
  python      958.2    947.1   972.7    0.8  2.72x slower     11.0        -
    ✓  output verified: 7/7 match oracle '425577'

gate: BIG WARN (fix this): path-join: cp3 352 ms is 1.43x rust (247 ms), tripwire 1.20x
gate: BIG WARN (fix this): path-join: cp3 352 ms is 4.13x go (85 ms), tripwire 1.00x
```
<!-- test:path-join:end -->

<!-- test:date-format:start -->
### date-format
_Updated 2026-10-30T17:45:22 by `python benchmarks/runner/run_benchmark.py --only map-churn,date-format,math-kernel --reps 7`._

```text
  * date-format  expected = 796897 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------------
  go          45.38    41.89   75.79  24.8  1.37x faster      7.3     1815
  cpp         56.88    52.03   64.24   8.2  1.09x faster      4.3       47
  cp3         62.04    61.04   226.1  72.3      baseline      3.7      454
  js          127.5    124.6   148.8   6.9  2.05x slower     40.8        -
  java        156.1    138.3   157.8   5.8  2.52x slower     54.1        2
  python      262.8    257.1   280.1   3.3  4.24x slower     11.1        -
    +  output verified: 6/6 match oracle '796897'

gate: WARN date-format: cp3 62 ms is 1.09x cpp (57 ms), watch 1.05x
gate: BIG WARN (fix this): date-format: cp3 62 ms is 1.37x go (45 ms), tripwire 1.00x
```
<!-- test:date-format:end -->

<!-- test:os-probe:start -->
### os-probe
_Updated 2026-10-30T17:36:03 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ os-probe  expected = 208981 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  java        229.2    225.3   296.7  11.1  1.39x faster     43.1        2
  cpp         312.6    303.7   407.6  11.6  1.02x faster      4.7       21
  cp3         318.9    297.3   576.0  29.4      baseline      4.1      444
  go          621.6    603.7  1022.7  22.3  1.95x slower     12.0     1758
  python      812.1    787.7   871.3   3.8  2.55x slower     14.2        -
  js         2467.4   2426.4  2509.2   1.4  7.74x slower     42.0        -
    ✓  output verified: 6/6 match oracle '208981'

gate: BIG WARN (fix this): os-probe: cp3 319 ms is 1.39x java (229 ms), tripwire 1.00x
```
<!-- test:os-probe:end -->

<!-- test:binary-trees:start -->
### binary-trees
_Updated 2026-10-30T17:36:15 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ binary-trees  expected = 1348958 | full binary trees to depth 13, recursive build and check | median/best/p95 ms; CV = varia…
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  rust        45.03    42.80   76.24  24.5  4.73x faster      4.2      124
  go          53.05    49.37   80.47  19.7  4.02x faster     11.7     1751
  cpp         77.66    76.35   106.6  13.4  2.75x faster      5.2       34
  java        94.04    91.09   108.3   7.4  2.27x faster     60.2        1
  js          98.28    91.69   104.3   4.7  2.17x faster     44.9        -
  cp3         213.2    206.4   364.7  28.2      baseline      6.0      440
  python      344.3    335.8   361.8   2.8  1.61x slower     12.5        -
    ✓  output verified: 7/7 match oracle '1348958'

gate: BIG WARN (fix this): binary-trees: cp3 213 ms is 2.75x cpp (78 ms), tripwire 1.20x
gate: BIG WARN (fix this): binary-trees: cp3 213 ms is 4.73x rust (45 ms), tripwire 1.20x
gate: BIG WARN (fix this): binary-trees: cp3 213 ms is 4.02x go (53 ms), tripwire 1.00x
gate: BIG WARN (fix this): binary-trees: cp3 213 ms is 2.17x js (98 ms), tripwire 1.00x
gate: BIG WARN (fix this): binary-trees: cp3 213 ms is 2.27x java (94 ms), tripwire 1.00x
```
<!-- test:binary-trees:end -->

<!-- test:map-churn:start -->
### map-churn
_Updated 2026-10-30T17:45:35 by `python benchmarks/runner/run_benchmark.py --only map-churn,date-format,math-kernel --reps 7`._

```text
  * map-churn  expected = 942471769 | 100,000-key hash map, delete/reinsert/update churn | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  rust        22.99    19.36   46.35   36.4  1.20x faster      6.0      128
  go          24.73    22.65   37.70   21.4  1.12x faster      8.1     1756
  cp3         27.60    25.62   197.4  123.6      baseline     11.7      444
  cpp         37.45    33.70   58.95   21.9  1.36x slower      8.9       21
  python      97.46    94.36   106.0    4.3  3.53x slower     24.0        -
  js          110.8    107.5   128.1    6.5  4.01x slower     47.0        -
  java        149.1    140.1   156.2    4.1  5.40x slower     69.9        1
    +  output verified: 7/7 match oracle '942471769'

gate: BIG WARN (fix this): map-churn: cp3 28 ms is 1.20x rust (23 ms), tripwire 1.20x
gate: BIG WARN (fix this): map-churn: cp3 28 ms is 1.12x go (25 ms), tripwire 1.00x
```
<!-- test:map-churn:end -->

<!-- test:utf8-text:start -->
### utf8-text
_Updated 2026-10-30T17:37:11 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ utf8-text  expected = 78156718 | 417,792-byte UTF-8 text, 32 manual decode rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  cpp         55.43    53.04   145.8   49.5  5.28x faster      5.0       21
  rust        59.03    58.37   398.3  118.8  4.96x faster      4.6      124
  go          63.12    60.00   321.1   99.1  4.64x faster      6.4     1756
  java        142.4    139.1   152.2    3.2  2.06x faster     42.1        2
  js          229.1    223.3   269.3    7.0  1.28x faster     40.8        -
  cp3         292.7    283.1   532.3   29.4      baseline      4.5      444
  python     2717.6   2686.4  2847.7    2.1  9.28x slower     12.6        -
    ✓  output verified: 7/7 match oracle '78156718'

gate: BIG WARN (fix this): utf8-text: cp3 293 ms is 5.28x cpp (55 ms), tripwire 1.20x
gate: BIG WARN (fix this): utf8-text: cp3 293 ms is 4.96x rust (59 ms), tripwire 1.20x
gate: BIG WARN (fix this): utf8-text: cp3 293 ms is 4.64x go (63 ms), tripwire 1.00x
gate: BIG WARN (fix this): utf8-text: cp3 293 ms is 1.28x js (229 ms), tripwire 1.00x
gate: BIG WARN (fix this): utf8-text: cp3 293 ms is 2.06x java (142 ms), tripwire 1.00x
```
<!-- test:utf8-text:end -->

<!-- test:regex-scan:start -->
### regex-scan
_Updated 2026-10-30T17:37:23 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ regex-scan  expected = 735703090 | 240,000-byte text, global [0-9]+ scan, 8 rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  js          87.56    81.97   112.1  13.2  1.13x faster     42.4        -
  cp3         99.11    88.33   319.8  62.0      baseline      6.0      826
  go          118.8    111.6   527.9  87.9  1.20x slower     12.3     2074
  python      125.0    111.3   135.4   6.4  1.26x slower     15.3        -
  java        147.6    133.2   157.9   6.5  1.49x slower     55.5        1
  cpp         152.9    148.4   304.9  33.0  1.54x slower      5.0      130
    ✓  output verified: 6/6 match oracle '735703090'

gate: BIG WARN (fix this): regex-scan: cp3 99 ms is 1.13x js (88 ms), tripwire 1.00x
```
<!-- test:regex-scan:end -->

<!-- test:json-parse-large:start -->
### json-parse-large
_Updated 2026-10-30T17:37:37 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ json-parse-large  expected = 613781 | 204,472-byte JSON record, 20 parse rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  js          101.0    95.37   133.0  12.0  4.08x faster     51.9        -
  python      156.9    153.4   209.0  11.8  2.62x faster     16.8        -
  go          160.0    151.0   460.8  56.2  2.57x faster     13.1     2154
  java        221.7    217.8   240.9   4.2  1.86x faster    131.6        3
  cp3         411.6    389.4   691.1  23.7      baseline     13.3      472
    ✓  output verified: 5/5 match oracle '613781'

gate: BIG WARN (fix this): json-parse-large: cp3 412 ms is 2.57x go (160 ms), tripwire 1.00x
gate: BIG WARN (fix this): json-parse-large: cp3 412 ms is 4.08x js (101 ms), tripwire 1.00x
gate: BIG WARN (fix this): json-parse-large: cp3 412 ms is 2.62x python (157 ms), tripwire 1.00x
gate: BIG WARN (fix this): json-parse-large: cp3 412 ms is 1.86x java (222 ms), tripwire 1.00x
```
<!-- test:json-parse-large:end -->

<!-- test:deep-recursion:start -->
### deep-recursion
_Updated 2026-10-30T17:37:46 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ deep-recursion  expected = 292249866 | 5,000-deep recursion, 200 rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  ──────────────────────────────────────────────────────────────────────────
  cp3          9.38     8.08   320.3  165.5       baseline      3.6      433
  rust        10.90     7.95   82.87  130.8   1.16x slower      4.1      123
  go          11.62    11.08   246.8  189.8   1.24x slower      6.1     1750
  cpp         12.30    10.69   66.43  103.9   1.31x slower      4.6       19
  java        75.83    74.27   102.5   12.5   8.09x slower     38.8        1
  js          76.53    73.24   92.56    8.5   8.16x slower     38.4        -
  python      189.6    178.2   209.6    6.3  20.22x slower     11.7        -
    ✓  output verified: 7/7 match oracle '292249866'
```
<!-- test:deep-recursion:end -->

<!-- test:exception-cost:start -->
### exception-cost
_Updated 2026-10-30T17:38:25 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ exception-cost  expected = 446742259 446742259 | 1,000,000 throw/catch vs sentinel-return iterations | median/best/p95 ms; C…
  lang    median ms  best ms  p95 ms  CV%        vs cp3  RSS MiB  bin KiB
  ───────────────────────────────────────────────────────────────────────
  java        327.0    307.1   388.3  8.0  4.24x faster    191.3        1
  python      485.5    474.3   508.9  2.4  2.85x faster     11.0        -
  cpp         736.9    726.7   815.0  4.5  1.88x faster      4.8       21
  cp3        1385.3   1361.4  1696.0  8.6      baseline      4.2      439
  js         2021.3   1956.1  2115.8  2.8  1.46x slower     40.3        -
    ✓  output verified: 5/5 match oracle '446742259 446742259'

gate: BIG WARN (fix this): exception-cost: cp3 1385 ms is 1.88x cpp (737 ms), tripwire 1.20x
gate: BIG WARN (fix this): exception-cost: cp3 1385 ms is 2.85x python (486 ms), tripwire 1.00x
gate: BIG WARN (fix this): exception-cost: cp3 1385 ms is 4.24x java (327 ms), tripwire 1.00x
```
<!-- test:exception-cost:end -->

<!-- test:file-big:start -->
### file-big
_Updated 2026-10-30T17:39:04 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,health-server,health-server-multi --reps 7`._

```text
  ◆ file-big  expected = 779913 | 8 MiB file, 8 write/read rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  js          245.4    233.0   275.0   5.6  2.60x faster    111.4        -
  java        298.9    288.6   336.8   5.5  2.14x faster    133.1        2
  rust        299.5    252.0  1155.6  77.7  2.13x faster     20.3      146
  cpp         339.9    281.4   462.8  17.4  1.88x faster     21.0       31
  go          388.3    284.4   741.0  34.7  1.65x faster     47.5     1891
  cp3         638.8    550.2   964.3  20.9      baseline     29.4     1350
  python      732.0    691.6   786.8   4.2  1.15x slower     37.7        -
    ✓  output verified: 7/7 match oracle '779913'

gate: BIG WARN (fix this): file-big: cp3 639 ms is 1.88x cpp (340 ms), tripwire 1.20x
gate: BIG WARN (fix this): file-big: cp3 639 ms is 2.13x rust (300 ms), tripwire 1.20x
gate: BIG WARN (fix this): file-big: cp3 639 ms is 1.65x go (388 ms), tripwire 1.00x
gate: BIG WARN (fix this): file-big: cp3 639 ms is 2.60x js (245 ms), tripwire 1.00x
gate: BIG WARN (fix this): file-big: cp3 639 ms is 2.14x java (299 ms), tripwire 1.00x
```
<!-- test:file-big:end -->
<!-- latest-by-test:end -->
