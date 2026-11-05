

## Latest results by test

<!-- latest-by-test:start -->
<!-- test:health-server:start -->
### health-server
_Updated 2026-11-04T20:22:33 by `python benchmarks/runner/run_benchmark.py --only health-server --reps 1 --latency 5 --seconds 3 --concurrency 2`._

```text
  * health-server  median boot/req/s; pooled latency p50/p95; CV = run-to-run variation %
  lang        ok/run  boot ms  p50 ms  p95 ms   req/s  RPS CV%  errors         vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------------------------------------------
  cpp            1/1    514.5    0.11    0.13  25,585        -       0   1.02x faster      5.3       60
  cp3            1/1    523.0    0.07    0.07  25,200        -       0       baseline      9.5     1622
  rust           1/1    531.1    0.07    0.07  24,686        -       0   1.02x slower      6.3     1098
  go             1/1    511.6    0.09    0.09  22,365        -       0   1.13x slower     20.6    15630
  js-fastify     1/1    517.9    0.13    0.19  21,706        -       0   1.16x slower     67.8        -
  java           1/1    587.6    0.27    0.31  21,328        -       0   1.18x slower    177.2        4
  dotnet         1/1    528.4    0.14    0.18  20,295        -       0   1.24x slower    193.5        8
  js             1/1    519.2    0.28    0.30  16,869        -       0   1.49x slower     62.3        -
  python         1/1   1056.2    0.42    0.54   1,819        -       0  13.86x slower      4.0        -
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
_Updated 2026-10-30T18:07:00 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ buffer-copy  expected = 688024 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  ──────────────────────────────────────────────────────────────────────────
  rust        21.99    19.25   38.16   27.9   1.37x faster      4.1      125
  cpp         26.65    22.89   125.6   78.4   1.13x faster      4.2       21
  cp3         30.02    27.93   215.4  108.7       baseline      3.9      450
  go          83.26    66.82   138.3   27.0   2.77x slower     47.2     1752
  python      98.44    78.35   134.1   20.3   3.28x slower     11.8        -
  java        180.6    173.5   185.3    2.0   6.02x slower    264.4        1
  js          303.7    294.7   328.5    3.6  10.12x slower    116.0        -
    ✓  output verified: 7/7 match oracle '688024'

gate: WARN buffer-copy: cp3 30 ms is 1.13x cpp (27 ms), watch 1.05x
gate: BIG WARN (fix this): buffer-copy: cp3 30 ms is 1.37x rust (22 ms), tripwire 1.20x
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
_Updated 2026-10-30T18:07:37 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ math-kernel  expected = 177047762 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  rust        43.64    39.37   71.74  23.7   1.70x faster      4.1      124
  cp3         74.02    73.16   251.5  67.3       baseline      3.9      442
  cpp         131.1    128.9   160.0   8.2   1.77x slower      4.1       20
  go          138.7    132.9   148.1   4.3   1.87x slower      5.8     1762
  js          173.8    165.4   181.8   3.1   2.35x slower     40.2        -
  java        182.7    178.3   189.7   2.1   2.47x slower     37.9        1
  python     3680.2   3643.5  3805.5   1.5  49.72x slower     11.0        -
    ✓  output verified: 7/7 match oracle '177047762'

gate: BIG WARN (fix this): math-kernel: cp3 74 ms is 1.70x rust (44 ms), tripwire 1.20x
```
<!-- test:math-kernel:end -->

<!-- test:word-frequency:start -->
### word-frequency
_Updated 2026-10-30T18:07:54 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ word-frequency  expected = 37425343 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  go          55.91    50.90   101.4  27.9  1.46x faster      5.8     1762
  cpp         72.92    71.16   94.31  10.7  1.12x faster      4.6       23
  rust        79.11    77.18   107.0  12.7  1.03x faster      3.9      130
  cp3         81.87    68.93   266.7  67.1      baseline      3.7      456
  java        170.0    166.5   193.4   5.2  2.08x slower     73.3        1
  js          284.4    270.1   309.8   5.2  3.47x slower     38.6        -
  python      814.2    798.6   842.0   2.1  9.95x slower     11.1        -
    ✓  output verified: 7/7 match oracle '37425343'

gate: WARN word-frequency: cp3 82 ms is 1.12x cpp (73 ms), watch 1.05x
gate: BIG WARN (fix this): word-frequency: cp3 82 ms is 1.46x go (56 ms), tripwire 1.00x
```
<!-- test:word-frequency:end -->

<!-- test:object-churn:start -->
### object-churn
_Updated 2026-10-30T18:08:24 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ object-churn  expected = 90641269 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  cp3         51.86    50.52   196.0  74.7       baseline     22.8      438
  go          85.33    80.05   102.0   8.7   1.65x slower     46.1     1751
  cpp         176.6    173.9   191.4   3.4   3.41x slower     61.9       20
  rust        179.9    174.7   205.3   6.2   3.47x slower     61.4      124
  java        208.7    193.4   243.4   7.9   4.02x slower    126.2        1
  js          537.0    498.6   608.3   6.4  10.35x slower    306.4        -
  python     2182.5   2134.4  2307.6   2.6  42.08x slower    233.2        -
    ✓  output verified: 7/7 match oracle '90641269'
```
<!-- test:object-churn:end -->

<!-- test:array-growth:start -->
### array-growth
_Updated 2026-10-30T18:08:55 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ array-growth  expected = 200000010000000 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  cpp         143.5    135.4   163.8   6.1   1.15x faster    261.1       20
  rust        151.9    137.5   167.3   7.0   1.08x faster    260.7      124
  cp3         164.4    156.1   310.0  33.8       baseline    156.2      434
  go          257.9    236.5   302.4   8.8   1.57x slower    567.9     1750
  java        269.5    265.3   286.8   2.9   1.64x slower    554.8        1
  js          425.7    396.1   440.6   3.7   2.59x slower    363.3        -
  python     2242.1   2135.6  2446.9   4.3  13.64x slower    829.8        -
    ✓  output verified: 7/7 match oracle '200000010000000'

gate: WARN array-growth: cp3 164 ms is 1.15x cpp (144 ms), watch 1.05x
gate: WARN array-growth: cp3 164 ms is 1.08x rust (152 ms), watch 1.05x
```
<!-- test:array-growth:end -->

<!-- test:closure-capture:start -->
### closure-capture
_Updated 2026-10-30T18:09:23 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ closure-capture  expected = 16666688333331 998521500000 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  rust        22.71    21.75   44.68  31.4   2.51x faster      3.7      125
  go          23.24    22.46   49.69  36.4   2.45x faster      5.9     1752
  cp3         56.93    52.37   224.6  77.3       baseline     80.0      442
  cpp         97.43    93.90   123.5  10.0   1.71x slower    182.2       21
  java        156.7    155.5   173.6   4.8   2.75x slower     78.6        2
  js         1011.8    989.0  1112.5   4.1  17.77x slower    293.1        -
  python     1827.1   1784.3  1847.5   1.3  32.10x slower     11.4        -
    ✓  output verified: 7/7 match oracle '16666688333331 998521500000'

gate: BIG WARN (fix this): closure-capture: cp3 57 ms is 2.51x rust (23 ms), tripwire 1.20x
gate: BIG WARN (fix this): closure-capture: cp3 57 ms is 2.45x go (23 ms), tripwire 1.00x
```
<!-- test:closure-capture:end -->

<!-- test:interface-dispatch:start -->
### interface-dispatch
_Updated 2026-10-30T18:09:55 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ interface-dispatch  expected = 37999986 66000000 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  go          63.83    60.44   81.80  11.0  6.29x faster     63.6     1754
  rust        150.6    147.4   159.8   3.3  2.67x faster     90.7      126
  cpp         175.9    169.2   211.6   8.0  2.28x faster    111.5       22
  java        223.8    213.2   248.6   6.4  1.80x faster    154.5        2
  cp3         401.7    358.0   595.4  21.1      baseline    213.2      452
  js          684.9    650.7   707.6   2.9  1.71x slower    388.8        -
  python     1974.3   1910.8  2051.5   2.6  4.91x slower    236.6        -
    ✓  output verified: 7/7 match oracle '37999986 66000000'

gate: BIG WARN (fix this): interface-dispatch: cp3 402 ms is 2.28x cpp (176 ms), tripwire 1.20x
gate: BIG WARN (fix this): interface-dispatch: cp3 402 ms is 2.67x rust (151 ms), tripwire 1.20x
gate: BIG WARN (fix this): interface-dispatch: cp3 402 ms is 6.29x go (64 ms), tripwire 1.00x
gate: BIG WARN (fix this): interface-dispatch: cp3 402 ms is 1.80x java (224 ms), tripwire 1.00x
```
<!-- test:interface-dispatch:end -->

<!-- test:generics-mono:start -->
### generics-mono
_Updated 2026-10-30T18:10:09 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ generics-mono  expected = 998986007 998986007 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  cpp         29.43    28.80   41.03  14.2   2.05x faster      4.1       19
  go          30.44    29.79   37.81   8.9   1.98x faster      5.7     1750
  rust        33.59    31.76   40.19   7.8   1.80x faster      3.8      124
  cp3         60.37    56.31   266.7  82.6       baseline      3.6      441
  js          112.3    104.1   181.0  22.1   1.86x slower     40.3        -
  java        155.6    146.4   165.0   4.3   2.58x slower     51.9        2
  python      784.3    773.9   828.1   2.3  12.99x slower     11.1        -
    ✓  output verified: 7/7 match oracle '998986007 998986007'

gate: BIG WARN (fix this): generics-mono: cp3 60 ms is 2.05x cpp (29 ms), tripwire 1.20x
gate: BIG WARN (fix this): generics-mono: cp3 60 ms is 1.80x rust (34 ms), tripwire 1.20x
gate: BIG WARN (fix this): generics-mono: cp3 60 ms is 1.98x go (30 ms), tripwire 1.00x
```
<!-- test:generics-mono:end -->

<!-- test:json-roundtrip:start -->
### json-roundtrip
_Updated 2026-10-30T18:10:24 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ json-roundtrip  expected = 664346 | 20,000 parse+stringify round trips of one 310-byte record | median/best/p95 ms; CV = var…
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  js          145.3    124.9   156.9   7.8  2.05x faster     40.6        -
  go          195.2    182.1   219.3   6.7  1.53x faster     13.2     2154
  java        284.7    267.4   300.7   3.7  1.05x faster    153.2        4
  cp3         297.9    289.8   510.9  28.4      baseline      3.9      492
  python      343.6    335.5   360.4   2.6  1.15x slower     12.6        -
    ✓  output verified: 5/5 match oracle '664346'

gate: BIG WARN (fix this): json-roundtrip: cp3 298 ms is 1.53x go (195 ms), tripwire 1.00x
gate: BIG WARN (fix this): json-roundtrip: cp3 298 ms is 2.05x js (145 ms), tripwire 1.00x
gate: BIG WARN (fix this): json-roundtrip: cp3 298 ms is 1.05x java (285 ms), tripwire 1.00x
```
<!-- test:json-roundtrip:end -->

<!-- test:async-fanout:start -->
### async-fanout
_Updated 2026-10-30T18:11:07 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ async-fanout  expected = 99898 | 10,000-timer fan-out joined at once, then 10,000 chained awaits | median/best/p95 ms; CV = …
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  ──────────────────────────────────────────────────────────────────────────
  cp3         18.23    15.58   223.2  126.7       baseline      7.9      926
  go          30.88    26.63   52.08   26.2   1.69x slower      8.7     1760
  js          94.83    87.66   132.7   19.9   5.20x slower     51.1        -
  java        282.3    259.2   316.1    7.9  15.49x slower     97.4        3
  python      298.4    286.3   345.1    6.4  16.37x slower     34.9        -
    ✓  output verified: 5/5 match oracle '99898'
```
<!-- test:async-fanout:end -->

<!-- test:env-injection:start -->
### env-injection
_Updated 2026-10-30T18:11:26 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ env-injection  expected = 133644 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  cp3         137.9    122.5   252.4  33.5      baseline      4.2      456
  java        144.3    134.2   157.7   5.3  1.05x slower     62.2        1
  rust        173.1    170.4   206.3   8.0  1.25x slower      4.1      126
  cpp         228.6    224.1   257.0   5.0  1.66x slower      4.1       21
  go          233.7    213.3   340.7  18.0  1.69x slower     12.0     1755
  js          416.4    411.3   434.1   1.9  3.02x slower     40.3        -
  python      471.7    465.2   495.2   2.3  3.42x slower     11.0        -
    ✓  output verified: 7/7 match oracle '133644'
```
<!-- test:env-injection:end -->

<!-- test:file-roundtrip:start -->
### file-roundtrip
_Updated 2026-10-30T18:12:00 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ file-roundtrip  expected = 698140 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  java        287.1    255.8   295.2   5.1  1.52x faster     43.5        2
  rust        325.8    313.4   363.3   5.3  1.34x faster      4.3      148
  go          357.0    316.2   540.8  20.8  1.22x faster      8.0     1900
  cpp         410.8    328.5   471.3  14.5  1.06x faster      5.0       33
  js          418.2    399.7   483.1   6.9  1.04x faster     40.9        -
  cp3         436.4    377.7   636.2  20.6      baseline      4.1     1352
  python      481.6    457.5   565.0   8.4  1.10x slower     13.9        -
    ✓  output verified: 7/7 match oracle '698140'

gate: WARN file-roundtrip: cp3 436 ms is 1.06x cpp (411 ms), watch 1.05x
gate: BIG WARN (fix this): file-roundtrip: cp3 436 ms is 1.34x rust (326 ms), tripwire 1.20x
gate: BIG WARN (fix this): file-roundtrip: cp3 436 ms is 1.22x go (357 ms), tripwire 1.00x
gate: BIG WARN (fix this): file-roundtrip: cp3 436 ms is 1.04x js (418 ms), tripwire 1.00x
gate: BIG WARN (fix this): file-roundtrip: cp3 436 ms is 1.52x java (287 ms), tripwire 1.00x
```
<!-- test:file-roundtrip:end -->

<!-- test:path-join:start -->
### path-join
_Updated 2026-10-30T18:12:29 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ path-join  expected = 425577 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  go          87.29    82.57   113.4  11.7  4.57x faster     11.7     1768
  rust        302.4    237.9   364.1  17.2  1.32x faster      4.1      135
  cp3         399.1    341.8   515.2  16.4      baseline      4.1      449
  java        421.1    396.3   508.0   9.0  1.06x slower    151.6        1
  js          506.4    485.2   606.1   7.9  1.27x slower     42.1        -
  cpp         605.4    594.2   630.4   2.2  1.52x slower      4.3       31
  python      965.5    953.3  1000.2   1.6  2.42x slower     11.0        -
    ✓  output verified: 7/7 match oracle '425577'

gate: BIG WARN (fix this): path-join: cp3 399 ms is 1.32x rust (302 ms), tripwire 1.20x
gate: BIG WARN (fix this): path-join: cp3 399 ms is 4.57x go (87 ms), tripwire 1.00x
```
<!-- test:path-join:end -->

<!-- test:date-format:start -->
### date-format
_Updated 2026-10-30T18:12:39 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ date-format  expected = 796897 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  go          37.50    36.42   43.73   7.7  1.59x faster      7.4     1815
  cpp         39.20    37.42   56.36  16.5  1.52x faster      4.6       47
  cp3         59.54    58.29   204.3  69.9      baseline      4.1      454
  js          108.3    106.0   127.0   6.7  1.82x slower     40.7        -
  java        140.0    125.3   144.3   4.8  2.35x slower     54.9        2
  python      240.0    237.3   251.7   2.0  4.03x slower     11.0        -
    ✓  output verified: 6/6 match oracle '796897'

gate: BIG WARN (fix this): date-format: cp3 60 ms is 1.52x cpp (39 ms), tripwire 1.20x
gate: BIG WARN (fix this): date-format: cp3 60 ms is 1.59x go (37 ms), tripwire 1.00x
```
<!-- test:date-format:end -->

<!-- test:os-probe:start -->
### os-probe
_Updated 2026-10-30T18:13:19 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ os-probe  expected = 208981 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  java        232.6    222.7   306.3  11.7  1.46x faster     43.2        2
  cpp         303.0    299.3   321.9   2.7  1.12x faster      4.6       21
  cp3         339.8    302.2   415.9  13.6      baseline      3.6      444
  go          664.1    621.2   758.7   7.4  1.95x slower     11.8     1758
  python      830.4    803.8   857.8   2.4  2.44x slower     14.2        -
  js         2530.6   2453.0  2694.2   2.9  7.45x slower     42.1        -
    ✓  output verified: 6/6 match oracle '208981'

gate: WARN os-probe: cp3 340 ms is 1.12x cpp (303 ms), watch 1.05x
gate: BIG WARN (fix this): os-probe: cp3 340 ms is 1.46x java (233 ms), tripwire 1.00x
```
<!-- test:os-probe:end -->

<!-- test:binary-trees:start -->
### binary-trees
_Updated 2026-11-04T19:02:00 by `python benchmarks/runner/run_benchmark.py --only binary-trees --reps 2`._

```text
  * binary-trees  expected = 1348958 | full binary trees to depth 13, recursive build and check | median/best/p95 ms; CV = vari..
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  java        58.19    58.16   58.21    0.1  4.07x faster     60.3        1
  cpp         61.25    46.05   76.45   35.1  3.87x faster      5.7       34
  js          62.89    60.31   65.47    5.8  3.77x faster     43.1        -
  go          89.22    28.77   149.7   95.8  2.66x faster     11.9     1751
  rust        142.5    26.80   258.1  114.8  1.66x faster      4.7      124
  python      198.0    192.0   203.9    4.3  1.20x faster     13.0        -
  cp3         237.0    218.9   255.1   10.8      baseline      7.2      442
    +  output verified: 7/7 match oracle '1348958'

gate: BIG WARN (fix this): binary-trees: cp3 237 ms is 3.87x cpp (61 ms), tripwire 1.20x
gate: BIG WARN (fix this): binary-trees: cp3 237 ms is 1.66x rust (142 ms), tripwire 1.20x
gate: BIG WARN (fix this): binary-trees: cp3 237 ms is 2.66x go (89 ms), tripwire 1.00x
gate: BIG WARN (fix this): binary-trees: cp3 237 ms is 3.77x js (63 ms), tripwire 1.00x
gate: BIG WARN (fix this): binary-trees: cp3 237 ms is 1.20x python (198 ms), tripwire 1.00x
gate: BIG WARN (fix this): binary-trees: cp3 237 ms is 4.07x java (58 ms), tripwire 1.00x
```
<!-- test:binary-trees:end -->

<!-- test:map-churn:start -->
### map-churn
_Updated 2026-10-30T18:13:39 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ map-churn  expected = 942471769 | 100,000-key hash map, delete/reinsert/update churn | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%        vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  rust        17.62    16.69   24.29   14.5  1.43x faster      6.0      128
  go          21.11    20.30   30.43   16.3  1.19x faster      8.1     1756
  cpp         23.46    22.32   39.42   24.2  1.07x faster      8.9       21
  cp3         25.21    24.24   178.5  108.6      baseline     11.5      444
  python      83.35    80.66   97.47    6.9  3.31x slower     23.9        -
  js          96.57    93.82   102.6    3.2  3.83x slower     46.4        -
  java        126.2    124.5   138.2    3.7  5.01x slower     69.8        1
    ✓  output verified: 7/7 match oracle '942471769'

gate: WARN map-churn: cp3 25 ms is 1.07x cpp (23 ms), watch 1.05x
gate: BIG WARN (fix this): map-churn: cp3 25 ms is 1.43x rust (18 ms), tripwire 1.20x
gate: BIG WARN (fix this): map-churn: cp3 25 ms is 1.19x go (21 ms), tripwire 1.00x
```
<!-- test:map-churn:end -->

<!-- test:utf8-text:start -->
### utf8-text
_Updated 2026-10-30T18:14:10 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ utf8-text  expected = 78156718 | 417,792-byte UTF-8 text, 32 manual decode rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  cpp         55.25    53.17   63.33   7.3  5.21x faster      4.5       21
  rust        61.31    59.47   68.45   4.8  4.69x faster      4.1      124
  go          63.32    58.48   74.28   9.1  4.54x faster      6.2     1756
  java        146.9    138.3   159.5   4.8  1.96x faster     42.2        2
  js          235.1    224.8   251.3   3.5  1.22x faster     40.9        -
  cp3         287.7    282.8   443.2  21.0      baseline      4.0      444
  python     2787.7   2700.4  2974.2   3.6  9.69x slower     12.6        -
    ✓  output verified: 7/7 match oracle '78156718'

gate: BIG WARN (fix this): utf8-text: cp3 288 ms is 5.21x cpp (55 ms), tripwire 1.20x
gate: BIG WARN (fix this): utf8-text: cp3 288 ms is 4.69x rust (61 ms), tripwire 1.20x
gate: BIG WARN (fix this): utf8-text: cp3 288 ms is 4.54x go (63 ms), tripwire 1.00x
gate: BIG WARN (fix this): utf8-text: cp3 288 ms is 1.22x js (235 ms), tripwire 1.00x
gate: BIG WARN (fix this): utf8-text: cp3 288 ms is 1.96x java (147 ms), tripwire 1.00x
```
<!-- test:utf8-text:end -->

<!-- test:regex-scan:start -->
### regex-scan
_Updated 2026-10-30T18:14:22 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ regex-scan  expected = 735703090 | 240,000-byte text, global [0-9]+ scan, 8 rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  cp3         93.90    89.33   261.2  58.4      baseline      5.6      826
  js          103.7    91.58   114.1   7.9  1.10x slower     44.2        -
  go          113.9    109.2   132.0   6.5  1.21x slower     11.9     2074
  python      119.7    112.5   143.7   9.8  1.28x slower     15.3        -
  java        155.9    155.2   192.2   9.6  1.66x slower     55.8        1
  cpp         156.8    152.3   164.7   2.7  1.67x slower      4.8      130
    ✓  output verified: 6/6 match oracle '735703090'
```
<!-- test:regex-scan:end -->

<!-- test:json-parse-large:start -->
### json-parse-large
_Updated 2026-10-30T18:14:35 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ json-parse-large  expected = 613781 | 204,472-byte JSON record, 20 parse rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  js          111.3    98.20   116.5   6.4  3.60x faster     51.9        -
  go          151.9    146.3   216.1  15.3  2.64x faster     13.1     2154
  python      154.5    147.7   167.7   4.4  2.59x faster     16.8        -
  java        224.2    215.8   242.7   3.8  1.79x faster    131.6        3
  cp3         400.5    391.0   631.4  21.5      baseline     13.3      472
    ✓  output verified: 5/5 match oracle '613781'

gate: BIG WARN (fix this): json-parse-large: cp3 401 ms is 2.64x go (152 ms), tripwire 1.00x
gate: BIG WARN (fix this): json-parse-large: cp3 401 ms is 3.60x js (111 ms), tripwire 1.00x
gate: BIG WARN (fix this): json-parse-large: cp3 401 ms is 2.59x python (154 ms), tripwire 1.00x
gate: BIG WARN (fix this): json-parse-large: cp3 401 ms is 1.79x java (224 ms), tripwire 1.00x
```
<!-- test:json-parse-large:end -->

<!-- test:deep-recursion:start -->
### deep-recursion
_Updated 2026-10-30T18:14:43 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ deep-recursion  expected = 292249866 | 5,000-deep recursion, 200 rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms    CV%         vs cp3  RSS MiB  bin KiB
  ──────────────────────────────────────────────────────────────────────────
  rust         8.25     7.37   22.29   51.0   1.50x faster      3.7      123
  cpp         11.13    10.28   18.69   24.1   1.11x faster      4.6       19
  cp3         12.36     9.15   163.4  131.0       baseline      2.4      433
  go          12.70    11.61   19.59   24.0   1.03x slower      5.9     1750
  js          75.57    72.30   88.63    7.1   6.12x slower     38.3        -
  java        85.44    76.19   95.16    7.9   6.92x slower     38.9        1
  python      188.5    180.2   247.1   12.3  15.25x slower     11.8        -
    ✓  output verified: 7/7 match oracle '292249866'

gate: WARN deep-recursion: cp3 12 ms is 1.11x cpp (11 ms), watch 1.05x
gate: note: deep-recursion: cp3 12.4 ms vs rust 8.2 ms is noise-scale, ratio not gated
```
<!-- test:deep-recursion:end -->

<!-- test:exception-cost:start -->
### exception-cost
_Updated 2026-10-30T18:15:22 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ exception-cost  expected = 446742259 446742259 | 1,000,000 throw/catch vs sentinel-return iterations | median/best/p95 ms; C…
  lang    median ms  best ms  p95 ms  CV%        vs cp3  RSS MiB  bin KiB
  ───────────────────────────────────────────────────────────────────────
  java        329.5    305.0   342.3  4.4  4.26x faster    190.9        1
  python      500.2    474.8   522.5  3.5  2.81x faster     11.0        -
  cpp         729.8    726.7   740.8  0.6  1.92x faster      4.7       21
  cp3        1403.7   1372.4  1631.6  7.3      baseline      3.9      439
  js         1979.0   1805.4  2133.6  5.5  1.41x slower     40.3        -
    ✓  output verified: 5/5 match oracle '446742259 446742259'

gate: BIG WARN (fix this): exception-cost: cp3 1404 ms is 1.92x cpp (730 ms), tripwire 1.20x
gate: BIG WARN (fix this): exception-cost: cp3 1404 ms is 2.81x python (500 ms), tripwire 1.00x
gate: BIG WARN (fix this): exception-cost: cp3 1404 ms is 4.26x java (329 ms), tripwire 1.00x
```
<!-- test:exception-cost:end -->

<!-- test:file-big:start -->
### file-big
_Updated 2026-10-30T18:15:56 by `python benchmarks/runner/run_benchmark.py --except typed-array-sum,sha256-churn,string-churn,hello-world,compute-single,compute-multi,compute-single-int,health-server,health-server-multi --reps 7`._

```text
  ◆ file-big  expected = 779913 | 8 MiB file, 8 write/read rounds | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%        vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  js          246.5    233.8   263.7   4.0  2.51x faster    111.2        -
  cpp         289.1    275.6   448.5  19.2  2.14x faster     20.5       31
  java        300.8    294.4   318.5   2.8  2.06x faster    126.5        2
  go          348.1    346.2   398.0   6.4  1.78x faster     47.4     1891
  rust        397.6    324.4   407.5  10.8  1.56x faster     19.8      146
  cp3         619.8    561.3   848.8  15.6      baseline     28.7     1350
  python      655.9    603.3   720.9   6.7  1.06x slower     37.7        -
    ✓  output verified: 7/7 match oracle '779913'

gate: BIG WARN (fix this): file-big: cp3 620 ms is 2.14x cpp (289 ms), tripwire 1.20x
gate: BIG WARN (fix this): file-big: cp3 620 ms is 1.56x rust (398 ms), tripwire 1.20x
gate: BIG WARN (fix this): file-big: cp3 620 ms is 1.78x go (348 ms), tripwire 1.00x
gate: BIG WARN (fix this): file-big: cp3 620 ms is 2.51x js (246 ms), tripwire 1.00x
gate: BIG WARN (fix this): file-big: cp3 620 ms is 2.06x java (301 ms), tripwire 1.00x
```
<!-- test:file-big:end -->

<!-- test:tree-walk:start -->
### tree-walk
_Updated 2026-11-04T19:04:08 by `python benchmarks/runner/run_benchmark.py --only tree-walk --reps 1`._

```text
  * tree-walk  expected = 16383
66134180 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms  CV%        vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------------
  cpp         41.24    41.24   41.24    -  2.65x faster      3.3       97
  js          49.95    49.95   49.95    -  2.19x faster     42.8        -
  python      50.34    50.34   50.34    -  2.17x faster     12.7        -
  java        73.32    73.32   73.32    -  1.49x faster     38.1        1
  cp3         109.3    109.3   109.3    -      baseline      5.7      440
  go          122.2    122.2   122.2    -  1.12x slower      5.1     1752
  rust            -        -       -    -             -        -        -
    x rust  build failed: error: linker `link.exe` not found
    +  output verified: 6/6 match oracle '16383\n66134180'

gate: BIG WARN (fix this): tree-walk: cp3 109 ms is 2.65x cpp (41 ms), tripwire 1.20x
gate: BIG WARN (fix this): tree-walk: cp3 109 ms is 2.19x js (50 ms), tripwire 1.00x
gate: BIG WARN (fix this): tree-walk: cp3 109 ms is 2.17x python (50 ms), tripwire 1.00x
gate: BIG WARN (fix this): tree-walk: cp3 109 ms is 1.49x java (73 ms), tripwire 1.00x
```
<!-- test:tree-walk:end -->

<!-- test:graph-bfs:start -->
### graph-bfs
_Updated 2026-11-04T20:07:35 by `python benchmarks/runner/run_benchmark.py --only graph-bfs --reps 1`._

```text
  * graph-bfs  cross-check only (no oracle yet) | graph-bfs | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms  CV%  vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------
  go          23.83    23.83   23.83    -       -      8.6     1751
  cpp         36.58    36.58   36.58    -       -      9.6       47
  java        144.5    144.5   144.5    -       -     71.8        1
  js          254.3    254.3   254.3    -       -     85.8        -
  python      645.2    645.2   645.2    -       -     33.1        -
  rust            -        -       -    -       -        -        -
  cp3             -        -       -    -       -        -        -
    x rust  build failed: error: linker `link.exe` not found
    ! cp3  not runnable yet: src/index.cp3:19:1: error: CP3401: type mismatch: cannot assign 'number' to 'u64'
    +  output verified: 5/5 match across languages
```
<!-- test:graph-bfs:end -->

<!-- test:strlist:start -->
### strlist
_Updated 2026-11-04T20:12:08 by `python benchmarks/runner/run_benchmark.py --only strlist --reps 1`._

```text
  * strlist  expected = 100000
92177148 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms  CV%  vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------
  cpp         21.82    21.82   21.82    -       -      2.4       47
  go          34.95    34.95   34.95    -       -     14.9     1751
  java        98.96    98.96   98.96    -       -     52.4        1
  js          259.1    259.1   259.1    -       -     83.4        -
  python      793.5    793.5   793.5    -       -     18.5        -
  rust            -        -       -    -       -        -        -
  cp3             -        -       -    -       -        -        -
    x rust  build failed: error: linker `link.exe` not found
    ! cp3  not runnable yet: src/index.cp3:8:1: error: CP3301: cannot find name 'f64'
    +  output verified: 5/5 match oracle '100000\n92177148'
```
<!-- test:strlist:end -->

<!-- test:interp:start -->
### interp
_Updated 2026-11-05T10:53:22 by `python benchmarks/runner/run_benchmark.py --only interp --reps 1`._

```text
  ◆ interp  expected = 24
914237 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms  CV%         vs cp3  RSS MiB  bin KiB
  ────────────────────────────────────────────────────────────────────────
  cpp         16.89    16.89   16.89    -  15.46x faster      2.1       48
  python      66.13    66.13   66.13    -   3.95x faster      4.1        -
  java        71.56    71.56   71.56    -   3.65x faster     37.6        1
  cp3         261.2    261.2   261.2    -       baseline      2.0     1229
  js              -        -       -    -              -        -        -
  goose           -        -       -    -              -        -        -
    ✗ js  run failed: file:///C:/Users/Krishnesh/dev/cp3/benchmarks/cases/dsa-object/interp/js/main.mjs:3
    ✗ goose  build failed: C:\Users\Krishnesh\dev\cp3\benchmarks\cases\dsa-object\interp\goose\main.goose:2: error: cannot find
             the module rng, looked for
    ✓  output verified: 4/4 match oracle '24\n914237'

gate: BIG WARN (fix this): interp: cp3 261 ms is 15.46x cpp (17 ms), tripwire 1.20x
gate: BIG WARN (fix this): interp: cp3 261 ms is 3.95x python (66 ms), tripwire 1.00x
gate: BIG WARN (fix this): interp: cp3 261 ms is 3.65x java (72 ms), tripwire 1.00x
```
<!-- test:interp:end -->

<!-- test:blur:start -->
### blur
_Updated 2026-11-04T20:10:15 by `python benchmarks/runner/run_benchmark.py --only blur --reps 1`._

```text
  * blur  expected = 256
7734030 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms  CV%         vs cp3  RSS MiB  bin KiB
  ------------------------------------------------------------------------
  go          10.28    10.28   10.28    -  11.79x faster      5.9     1750
  cpp         10.49    10.49   10.49    -  11.55x faster      2.1       49
  js          63.74    63.74   63.74    -   1.90x faster     40.6        -
  java        88.49    88.49   88.49    -   1.37x faster     38.6        1
  cp3         121.1    121.1   121.1    -       baseline      2.0      439
  python      397.4    397.4   397.4    -   3.28x slower     12.4        -
  rust            -        -       -    -              -        -        -
    x rust  build failed: error: linker `link.exe` not found
    +  output verified: 6/6 match oracle '256\n7734030'

gate: BIG WARN (fix this): blur: cp3 121 ms is 11.55x cpp (10 ms), tripwire 1.20x
gate: BIG WARN (fix this): blur: cp3 121 ms is 11.79x go (10 ms), tripwire 1.00x
gate: BIG WARN (fix this): blur: cp3 121 ms is 1.90x js (64 ms), tripwire 1.00x
gate: BIG WARN (fix this): blur: cp3 121 ms is 1.37x java (88 ms), tripwire 1.00x
```
<!-- test:blur:end -->

<!-- test:calc:start -->
### calc
_Updated 2026-11-05T13:25:50 by `python benchmarks/runner/run_benchmark.py --only calc,particles,records,scene --reps 1`._

```text
  * calc  expected = 3750
1250
580458 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms  CV%        vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------------
  python      92.17    92.17   92.17    -  3.12x faster     11.2        -
  js          98.41    98.41   98.41    -  2.92x faster     43.8        -
  cpp         100.6    100.6   100.6    -  2.86x faster      2.6       54
  java        120.4    120.4   120.4    -  2.39x faster     41.8        2
  goose       154.8    154.8   154.8    -  1.86x faster      2.1       58
  cp3         287.5    287.5   287.5    -      baseline      4.9     1242
  go          349.1    349.1   349.1    -  1.21x slower      5.3     1754
  rust            -        -       -    -             -        -        -
    x rust  build failed: error: linker `link.exe` not found
    +  output verified: 7/7 match oracle '3750\n1250\n580458'

gate: BIG WARN (fix this): calc: cp3 288 ms is 2.86x cpp (101 ms), tripwire 1.20x
gate: BIG WARN (fix this): calc: cp3 288 ms is 2.92x js (98 ms), tripwire 1.00x
gate: BIG WARN (fix this): calc: cp3 288 ms is 3.12x python (92 ms), tripwire 1.00x
gate: BIG WARN (fix this): calc: cp3 288 ms is 2.39x java (120 ms), tripwire 1.00x
```
<!-- test:calc:end -->

<!-- test:particles:start -->
### particles
_Updated 2026-11-05T13:26:04 by `python benchmarks/runner/run_benchmark.py --only calc,particles,records,scene --reps 1`._

```text
  * particles  expected = 5000
28315536 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms  CV%        vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------------
  java        102.7    102.7   102.7    -  2.76x faster     38.0        1
  cpp         104.9    104.9   104.9    -  2.70x faster      3.2       45
  js          107.0    107.0   107.0    -  2.65x faster     39.2        -
  goose       113.2    113.2   113.2    -  2.50x faster      3.5       58
  cp3         283.0    283.0   283.0    -      baseline      5.1     1235
  go          286.6    286.6   286.6    -  1.01x slower      2.1     1751
  python      491.4    491.4   491.4    -  1.74x slower     11.9        -
  rust            -        -       -    -             -        -        -
    x rust  build failed: error: linker `link.exe` not found
    +  output verified: 7/7 match oracle '5000\n28315536'

gate: BIG WARN (fix this): particles: cp3 283 ms is 2.70x cpp (105 ms), tripwire 1.20x
gate: BIG WARN (fix this): particles: cp3 283 ms is 2.65x js (107 ms), tripwire 1.00x
gate: BIG WARN (fix this): particles: cp3 283 ms is 2.76x java (103 ms), tripwire 1.00x
```
<!-- test:particles:end -->

<!-- test:push:start -->
### push
_Updated 2026-11-04T20:03:02 by `python benchmarks/runner/run_benchmark.py --only push --reps 1`._

```text
  * push  expected = 1000000
15625
499758894813
750050 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms  CV%        vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------------
  cpp         14.37    14.37   14.37    -  5.08x faster      2.1       46
  go          17.07    17.07   17.07    -  4.28x faster      2.1     1752
  cp3         72.99    72.99   72.99    -      baseline     15.7      446
  java        74.97    74.97   74.97    -  1.03x slower     80.5        1
  js          283.2    283.2   283.2    -  3.88x slower    296.6        -
  python      527.0    527.0   527.0    -  7.22x slower    112.5        -
  rust            -        -       -    -             -        -        -
    x rust  build failed: error: linker `link.exe` not found
    +  output verified: 6/6 match oracle '1000000\n15625\n499758894813\n750050'

gate: BIG WARN (fix this): push: cp3 73 ms is 5.08x cpp (14 ms), tripwire 1.20x
gate: BIG WARN (fix this): push: cp3 73 ms is 4.28x go (17 ms), tripwire 1.00x
```
<!-- test:push:end -->

<!-- test:records:start -->
### records
_Updated 2026-11-05T13:26:18 by `python benchmarks/runner/run_benchmark.py --only calc,particles,records,scene --reps 1`._

```text
  * records  expected = 100000
17463411840 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms  CV%        vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------------
  cpp         87.59    87.59   87.59    -  3.26x faster      2.1       47
  java        106.6    106.6   106.6    -  2.68x faster     43.5        2
  goose       106.8    106.8   106.8    -  2.67x faster      3.5       58
  js          153.3    153.3   153.3    -  1.86x faster     67.7        -
  python      235.0    235.0   235.0    -  1.21x faster     25.6        -
  cp3         285.4    285.4   285.4    -      baseline     14.7     1236
  go          382.9    382.9   382.9    -  1.34x slower     11.3     1752
  rust            -        -       -    -             -        -        -
    x rust  build failed: error: linker `link.exe` not found
    +  output verified: 7/7 match oracle '100000\n17463411840'

gate: BIG WARN (fix this): records: cp3 285 ms is 3.26x cpp (88 ms), tripwire 1.20x
gate: BIG WARN (fix this): records: cp3 285 ms is 1.86x js (153 ms), tripwire 1.00x
gate: BIG WARN (fix this): records: cp3 285 ms is 1.21x python (235 ms), tripwire 1.00x
gate: BIG WARN (fix this): records: cp3 285 ms is 2.68x java (107 ms), tripwire 1.00x
```
<!-- test:records:end -->

<!-- test:scene:start -->
### scene
_Updated 2026-11-05T13:26:32 by `python benchmarks/runner/run_benchmark.py --only calc,particles,records,scene --reps 1`._

```text
  * scene  expected = 23
4747 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms  CV%        vs cp3  RSS MiB  bin KiB
  -----------------------------------------------------------------------
  python      49.87    49.87   49.87    -  4.40x faster     11.0        -
  js          78.06    78.06   78.06    -  2.81x faster     33.9        -
  cpp         82.42    82.42   82.42    -  2.66x faster      4.6       50
  java        82.69    82.69   82.69    -  2.65x faster     38.8        2
  goose       98.72    98.72   98.72    -  2.22x faster      3.4       58
  cp3         219.2    219.2   219.2    -      baseline      5.4     1238
  go          316.8    316.8   316.8    -  1.45x slower      4.2     1752
  rust            -        -       -    -             -        -        -
    x rust  build failed: error: linker `link.exe` not found
    +  output verified: 7/7 match oracle '23\n4747'

gate: BIG WARN (fix this): scene: cp3 219 ms is 2.66x cpp (82 ms), tripwire 1.20x
gate: BIG WARN (fix this): scene: cp3 219 ms is 2.81x js (78 ms), tripwire 1.00x
gate: BIG WARN (fix this): scene: cp3 219 ms is 4.40x python (50 ms), tripwire 1.00x
gate: BIG WARN (fix this): scene: cp3 219 ms is 2.65x java (83 ms), tripwire 1.00x
```
<!-- test:scene:end -->

<!-- test:respond:start -->
### respond
_Updated 2026-11-05T13:37:14 by `python benchmarks/runner/run_benchmark.py --only respond`._

```text
  ◆ respond  expected = 578079
3348416444655930007 | cross-language checksum | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  ─────────────────────────────────────────────────────────────────────────
  goose       10.15     9.69   10.58  22.8   9.16x faster      4.8       58
  cpp         14.40    12.47   55.70  66.6   6.46x faster      5.9       50
  go          16.61    13.94   19.69  19.0   5.60x faster     10.2     1756
  cp3         93.03    84.91   372.4  75.2       baseline      7.1     1243
  java        95.31    91.07   111.8   8.9   1.02x slower     43.5        2
  js          138.8    133.8   156.8   7.4   1.49x slower     60.1        -
  python     1468.6   1061.7  2489.9  33.7  15.79x slower      4.1        -
  rust            -        -       -     -              -        -        -
    ✗ rust  build failed: error: linker `link.exe` not found
    ✓  output verified: 7/7 match oracle '578079\n3348416444655930007'

gate: BIG WARN (fix this): respond: cp3 93 ms is 6.46x cpp (14 ms), tripwire 1.20x
gate: BIG WARN (fix this): respond: cp3 93 ms is 5.60x go (17 ms), tripwire 1.00x
```
<!-- test:respond:end -->

<!-- test:sum-array:start -->
### sum-array
_Updated 2026-11-04T19:02:29 by `python benchmarks/runner/run_benchmark.py --only sum-array --reps 2`._

```text
  * sum-array  expected = 4000000
15987891712 | median/best/p95 ms; CV = variation %
  lang    median ms  best ms  p95 ms   CV%         vs cp3  RSS MiB  bin KiB
  -------------------------------------------------------------------------
  cpp         23.88    21.54   26.22  13.8   4.44x faster     19.3       45
  go          27.61    25.14   30.07  12.6   3.84x faster     22.0     1750
  java        82.41    82.30   82.53   0.2   1.29x faster     54.9        1
  cp3         106.0    81.58   130.4  32.6       baseline     37.2      440
  js          445.9    441.3   450.4   1.4   4.21x slower     54.8        -
  python     2134.4   2123.5  2145.4   0.7  20.14x slower    158.3        -
  rust            -        -       -     -              -        -        -
    x rust  build failed: error: linker `link.exe` not found
    +  output verified: 6/6 match oracle '4000000\n15987891712'

gate: BIG WARN (fix this): sum-array: cp3 106 ms is 4.44x cpp (24 ms), tripwire 1.20x
gate: BIG WARN (fix this): sum-array: cp3 106 ms is 3.84x go (28 ms), tripwire 1.00x
gate: BIG WARN (fix this): sum-array: cp3 106 ms is 1.29x java (82 ms), tripwire 1.00x
```
<!-- test:sum-array:end -->
<!-- latest-by-test:end -->
