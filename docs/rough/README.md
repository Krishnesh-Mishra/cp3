# docs/rough - internal process docs

Everything in this folder is project-internal **rough work**: process
documents and the original sources the published `docs/` tree is split from.
It lives here - not in the repository root - on purpose: the root holds only
`README.md`, `.gitignore`, `CMakeLists.txt` and `benchmarks/` (the one tooling
exception, D66), and **new rough work, notes and plans go here too**.

| File | What it is |
| --- | --- |
| `DECISIONS.md` | Binding numbered decisions (`D1…`); wins over the roadmap on any conflict. |
| `M2-PLAN.md` | The living wave roadmap, open-bug ledger and parked ambiguities. |
| `STYLE.md` | Code layout, naming and budget rules (enforced by `tools/lint_layout.py`). |
| `ARCHITECTURE.md` | Module map, directory ownership, build rules. |
| `README (6).md` | The original single-file **language spec** source. |
| `CLI-README (5).md` | The original **CLI & tooling** spec source. |
| `STDLIB-README (4).md` | The original **standard library** spec source. |
| `CP3UI-README (1).md` | The separate `cp3ui` companion spec (deliberately not part of the docs tree). |

The three spec sources are split into `docs/language-spec/`,
`docs/cli-tooling/` and `docs/standard-library/` by
`docs/local-docs-viewer/scripts/split-docs.mjs` (run from
`docs/local-docs-viewer/`).

Code comments cite these files by bare name - e.g. "STYLE.md §8",
"DECISIONS.md D55" - that means this folder.

**House style (D66.4):** no em dashes (U+2014) or en dashes (U+2013) anywhere
in repo text: docs, code comments, agent instructions. Write `-` or rephrase.
