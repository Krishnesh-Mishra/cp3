# Benchmark cases

Cases are grouped by category; the runner finds a task via `tasks.find_task_dir` so either `cases/<task>/` or `cases/<category>/<task>/` works.

- `compute/` — pure cpu / loops / dispatch (10)
- `dsa-array/` — vectors, buffers, typed arrays (array-growth, buffer-copy, typed-array-sum, sum-array, file-big, blur)
- `dsa-object/` — objects, trees, scene, interpreter objects (object-churn, binary-trees, tree-walk, scene, interp, records)
- `dsa-string/` — string/word scans (string-churn, utf8-text, regex-scan, word-frequency, strlist, sexp)
- `dsa-map/` — hash maps, caches, graphs (map-churn, lru, graph-bfs, push)
- `stdlib/` — one row per stdlib module (sha256, math, os, date, env, file, path, json, respond, calc, particles)
- `memory/` — allocation-heavy fallback (calc, particles)
- `network/` — HTTP server rows (health-server, health-server-multi)

Add a task: create `cases/<category>/<task>/` with per-language folders + `README.md`, then add one line to `benchmarks/runner/tasks.py` (compute/memory list + `CATEGORIES`). Do not edit `run_suite.py`.
