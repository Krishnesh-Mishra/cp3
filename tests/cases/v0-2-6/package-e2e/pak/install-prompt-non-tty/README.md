# pak/install-prompt-non-tty

Pins the **non-tty/default** path of `cp3c install` for `.cp3pak` packages:
in an invocation without a terminal on stdin, the installer must not ask
(and must default to keeping the source `.cp3pak`).

What `case.json` pins here:

1. `cp3c build src/main.cp3` in `pkg/` produces the new-format pak.
2. `cp3c install ../pkg/.cp3/out/pak_promptkeep.cp3pak` in `app/` exits 0,
   prints `installed`, and the **source pak still exists afterwards**
   (`filesExist` on `../pkg/.cp3/out/pak_promptkeep.cp3pak`, resolved against
   the install step's cwd). This is the strongest harness-expressible pin:
   it fails if a non-tty install deletes or consumes its source archive.
3. The consumer app runs and prints `beacon` / `5`.

What is deliberately NOT pinned: the text of any prompt question. The
non-tty path must never print the question, and on a tty the installer asks
something like "keep the .cp3pak? [y/N]" - but the exact wording belongs to the
fix, which had not landed when this fixture was written. A guessed substring
would false-fail a correct implementation or quietly pin the wrong thing, so
this fixture does not guess. If you (the maintainer) know the landed prompt
wording, add `stderrNotContains: ["<exact question>"]` to the install step -
that assertion is safe once the wording exists.

## Manual verification for the maintainer (interactive path)

Not exercisable in this harness (the runner never allocates a tty). Verify
by hand, in a real terminal:

```bash
cd <some scratch project>
cp3c build src/main.cp3        # in a copy of pkg/
cp3c install pkg/.cp3/out/pak_promptkeep.cp3pak
```

1. The question is printed **only when stdin is a tty**.
2. Answering `y` behaves as the landed spec intends (e.g. the source pak is
   kept or removed - match the spec, not this README).
3. Answering `n` (or the default) keeps the source pak.
4. In a non-tty session (`cp3c install ... < /dev/null`) no question is
   printed and the source pak is kept - this is what `case.json` pins.

Note on stdin: `runner.py` does not pipe stdin, it inherits the invoking
process's stdin. When the suite is run from CI or a non-interactive shell it
is not a tty; if you run it from an interactive terminal, redirect stdin
(`... < /dev/null` on POSIX, `< nul` on Windows) to reproduce the non-tty
invocation faithfully. The pinned behavior (no prompt, pak kept) must hold
either way.
