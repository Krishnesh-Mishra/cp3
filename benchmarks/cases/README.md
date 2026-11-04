# Benchmark cases

Cases are grouped by category; the runner finds a task via `tasks.find_task_dir` so either `cases/<task>/` or `cases/<category>/<task>/` works.

- `compute/` — cpu/math/string/loop kernels
- `network/` — HTTP server rows (health-server, health-server-multi)
- `memory/` — allocation-heavy DSA (trees, graphs, lists, caches, parsers)
- `stdlib/` — one-row-per-stdlib-module probes

Add a task: create `cases/<category>/<task>/` with per-language folders + `README.md`, then add one line to `benchmarks/runner/tasks.py` (compute/memory list + `CATEGORIES`). Do not edit `run_suite.py`.
