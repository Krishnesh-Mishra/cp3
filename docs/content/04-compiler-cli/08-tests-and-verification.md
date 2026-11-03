---
title: Tests and compiler verification
chapter: Chapter 4 · Compiler and tools
---
# Tests and compiler verification

CP3 has several verification layers. A parser or semantic test does not prove that generated C++ compiles; a successful build does not prove runtime output, edge cases, or diagnostics. Choose tests that match the changed layer and keep the verified claim precise.

## The conformance runner

Run the repository's test harness from the project root:

```powershell
python tests/runner/run_tests.py
```

The runner supports two case layouts under `tests/cases`: a named `main.cp3` paired with `output.json` in a case directory, and a flat `<name>.cp3` paired with `<name>.expect.json`. The fixed-name layout remains common in older families. Both use JSON assertions; read `tests/cases/README.md` and the runner discovery code before adding a fixture. Cases may also include helper modules alongside the entry source.

## Check-mode cases

A `mode: "check"` case validates front-end behavior without generating and executing a native program. These are suitable for parser acceptance, module lookup, type rules, and diagnostics. They cannot prove that codegen emits valid C++ or that runtime behavior is correct. In the fixed-name layout, the check case lives in its own directory; in the flat layout it is identified by its sibling expectation file.

An error case should pin the diagnostic code and relevant source location or message fragment where the harness supports it. Prefer an assertion that describes the public failure contract instead of the entire incidental wording.

## Run-mode cases

A `mode: "run"` case builds and executes the program, then checks observable output and process status. Use these for code generation, runtime helpers, and standard-library behavior. Include empty, boundary, and error cases when those are part of the API contract. The fixed-name `output.json` format and flat `.expect.json` format encode the same kind of expected result; do not move cases between layouts without checking any helper-file path assumptions.

A successful run fixture exercises the configured native compiler. If a feature is expected to fail at a specific stage, assert that stage and its source diagnostic rather than accepting a later C++ compile failure.

## Focused filters and jobs

Use the runner's `--filter` argument to select a relevant category or path, and `--no-bench` when you only want correctness cases. For example:

```powershell
python tests/runner/run_tests.py --filter typed-arrays --no-bench
python tests/runner/run_tests.py --filter native_strict --no-bench
```

Use `--jobs 1` when isolating ordering, resource, or intermittent issues. Use `--verbose` to see each case. Check `python tests/runner/run_tests.py --help` for the exact options in the current checkout; do not copy a stale command from an old report.

## Add tests beside the feature family

Find the closest existing test family before creating a new path. Keep one concern per case where possible: valid form, invalid form, boundary value, and observable runtime result should be independently understandable. Related helper files should stay in the same case directory if the harness expects them there.

Do not move an existing test or edit its expected output just to make a change pass. If intended semantics change, update the language contract and the affected test deliberately, and explain why the old expectation is no longer correct.

## Compiler build is a separate gate

Build the CP3 compiler with the documented CMake build tree:

```powershell
cmake --build build
```

CMake reuses object files when their inputs remain current. It may reconfigure when `CMakeLists.txt`, configure dependencies, or documentation globs change. A CMake build verifies compilation and linking of the compiler; it does not run the conformance suite.

## Performance verification is separate

Performance changes need unchanged benchmark source, a compiler rebuilt with the intended code, verified output, repeated measurements, and generated-code evidence for the claimed transformation. Use the benchmark runner's named target and repetitions, for example:

```powershell
python benchmarks/runner/run_benchmark.py --only typed-array-sum --reps 7
```

Do not run server or network benchmark suites when the task only concerns local CPU kernels. Report median and variation, not only the best run. Keep benchmark results separate from correctness conclusions.

## Recommended validation sequence for a compiler patch

1. Build the compiler once after the implementation changes are ready.
2. Run the focused check-mode and run-mode cases for the affected feature.
3. Inspect generated C++ if code generation or optimization changed.
4. Run any explicitly requested benchmarks only after correctness checks or according to the task's agreed order.
5. Run the full suite once before release when required.
6. Report the exact commands and whether each passed, failed, or was not run.

This sequence avoids treating a successful compile or one green test as broad proof. If the user gives a different required order, follow that order.

## Related pages

- [Compilation pipeline](02-compilation-pipeline.md)
- [Diagnostics](06-diagnostics.md)
- [Generated C++ inspection](10-generated-cpp-inspection.md)
- [Optimization pipeline](03-optimization-pipeline.md)
