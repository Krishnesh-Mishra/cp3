# pak/closure-excludes-unreferenced

Pins that the `.cp3pak` source package ships **exactly** its relative-import
closure, not the whole `src/` tree.

- `pkg/src/main.cp3` - the entry; imports `./used.cp3`.
- `pkg/src/used.cp3` - imported by the entry; must be in the closure.
- `pkg/src/unused.cp3` - never imported anywhere; must NOT be in the closure
  and must NOT be extractable/installed from the pak.

## Why the exact member list is a manual check

`tests/v0.2.5/package-test/runner.py` has no archive-inspect step type
(supported actions: `expectExit`, `stdoutContains`/`stdoutNotContains`,
`stderrContains`/`stderrNotContains`, `filesExist`/`filesAbsent`,
`fileContains`/`fileNotContains`, `repeat`). There is no way to list archive
members from `case.json`, and `fileContains` cannot see inside a gzip
container. So:

1. `case.json` asserts the pack succeeds, the install succeeds, the installed
   tree carries `src/main.cp3` and `src/used.cp3` and NOT `src/unused.cp3`
   (install unpacks members at their archive-relative paths - the
   harness-expressible proxy for the member list), and the consumer runs and
   prints `21` (which also proves `used.cp3` is really in the closure).
2. The exact archive member list is a **manual check for the maintainer**
   (run after `cp3c build src/main.cp3` in the `pkg/` folder):

   ```bash
   python -c "import tarfile; [print(m.name) for m in tarfile.open('tests/cases/v0-2-6/package-e2e/pak/closure-excludes-unreferenced/pkg/.cp3/out/pak_closure.cp3pak', 'r:gz').getmembers()]"
   ```

   Expected: the entry
   plus its relative-import closure at package-relative paths -
   `src/main.cp3`, `src/used.cp3` - plus the container members
   (`cp3pak.json`, `package.json`). `src/unused.cp3` must NOT appear, and no
   path outside the closure may appear. This depends on the v0.2.6 member
   convention (verbatim `.cp3` sources at exact folder structure); if the
   landed fix keeps a generated surface file too, that extra member is
   allowed - the pin is about `src/unused.cp3`, nothing else.
3. `case.json`'s three install-step paths (`.cp3/packages/pak_closure/src/...`)
   encode the same folder-structure assumption. If the landed layout flattens
   members (e.g. drops the `src/` prefix), the pack will still succeed but the
   install step will fail on those paths - that is a fixture-layout mismatch,
   not a closure bug: adjust the three paths, keep the intent, and re-pin the
   manual check above accordingly.
