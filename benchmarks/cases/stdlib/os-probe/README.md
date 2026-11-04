# os-probe - query platform, CPU count, home, temp and hostname 50,000 times

Operating-system surface workload, identical in all five participating
languages. Each iteration queries the same five facts:

1. platform name (`windows` / `macos` / `linux`)
2. logical CPU count
3. home directory
4. temp directory
5. hostname

The platform maps to `1`/`2`/`3` and becomes a "documented platform" boolean;
the CPU count and the three strings become non-empty/positive booleans. Fold
only those booleans, weighted `16/8/4/2/1`:

```text
term     = platform_ok * 16 + cpu_ok * 8 + home_ok * 4 + temp_ok * 2 + host_ok
checksum = (checksum * 31 + term) % 1000003
```

One line, one integer: `208981`.

No path, hostname or core count is folded, so the checksum is identical on
every machine and every supported OS. The task measures the repeated probe
cost, not the machine's identity.

## Language notes

- cpp: `std::thread::hardware_concurrency`, `std::getenv` for
  `USERPROFILE`/`HOME`, `TEMP`/`TMP` and `COMPUTERNAME`/`HOSTNAME`.
- go: `runtime.GOOS`, `runtime.NumCPU`, `os.UserHomeDir`, `os.TempDir`,
  `os.Hostname`.
- js: `node:os` `platform`, `availableParallelism`, `homedir`, `tmpdir`,
  `hostname`.
- python: `sys.platform`, `os.cpu_count`, `os.path.expanduser`,
  `tempfile.gettempdir`, `socket.gethostname`.
- cp3: `std/os` `platform`, `cpus`, `homedir`, `tmpdir`, `hostname`.
- rust: **omitted** - Rust `std` has no hostname/platform probe without
  `libc` or a crate; hand-rolling the bindings would compare dependencies.

## Run one by hand

From each language's folder:

```bash
# cpp
g++ -std=c++20 -O3 -o os-probe.exe main.cpp
./os-probe.exe

# go
go build -o os-probe.exe main.go
./os-probe.exe

# js
node main.mjs

# python
python main.py

# cp3
build/bin/cp3c.exe build benchmarks/cases/os-probe/cp3/src/index.cp3
build/bin/cp3c.exe run benchmarks/cases/os-probe/cp3/src/index.cp3
```

The oracle value is re-derived by `../../runner/oracle.py` (`os_probe_checksum`):

```bash
python benchmarks/runner/oracle.py
```

Rerun the tier with `python benchmarks/runner/run_benchmark.py --only os-probe`.
