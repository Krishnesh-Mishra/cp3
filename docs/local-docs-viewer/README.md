# C+++ Local Docs Viewer

A tiny local web app for reading the C+++ documentation in the parent `docs/`
folder. Zero build step — it renders the Markdown on the fly with
[`marked`](https://github.com/markedjs/marked) and highlights code with
[`highlight.js`](https://highlightjs.org/).

## Run it

```bash
cd docs/local-docs-viewer
npm install       # first time only
npm start
```

Then open **<http://localhost:3000>**.

Use a different port with:

```bash
# PowerShell
$env:PORT=4000; npm start
# macOS / Linux
PORT=4000 npm start
```

## What you get

- A **landing page** with a **"From TypeScript to C+++ in 2 days"** starter band up
  top, then one card per topic previewing its sections.
- A **collapsible sidebar** — topic groups expand only for the page you're on
  (and the **Start Here** group stays open), so the nav stays short instead of
  listing all 48 pages at once.
- A **sticky top bar** with breadcrumbs, previous/next page buttons, a "view raw
  Markdown" link, and the theme toggle.
- **As-you-type search** (press `/` to focus) with keyboard navigation through
  results, plus a no-JS `/search` results page as a fallback.
- A **sticky "On this page" table of contents** on wide screens (collapses to a
  box on narrower ones) with scroll-spy highlighting.
- **Syntax-highlighted code** with a hover **Copy** button, anchor links on
  headings, and back-to-top.
- **Light/dark theme** that remembers your choice and respects your OS setting.
- **Apple system palette** — the light/dark colours are Apple's own tokens
  (`systemBackground` `#ffffff` / `#000000`, surfaces `#f5f5f7` / `#1c1c1e`,
  labels `#1d1d1f` / `#f5f5f7`, and `systemBlue` `#0071e3` → `#2997ff`).
- **Translucent materials & scroll edges** — the top bar and sidebar are frosted
  layers (`backdrop-filter`) that content scrolls under, rather than opaque bars.
- **A fluid, interruptible navigation drawer** on mobile: it tracks your finger
  1:1 (respecting where you grabbed it), carries its release velocity into a
  spring, projects momentum to decide open vs. close, rubber-bands at the edges,
  and can be grabbed again mid-flight.
- **Reduced-motion / reduced-transparency / higher-contrast** are all respected:
  slides become cross-fades, frosted surfaces become solid, and borders firm up.
- Cross-references between documents (`README.md §14`, `CLI-README.md §2`, bare
  `§9`, …) are rewritten into real links while the Markdown is generated, so
  they work here *and* in any other Markdown viewer (GitHub, VS Code, …).

## The `docs/` tree

```
docs/
├── index.md                    # landing page
├── getting-started/            # hand-written — the 2-day TS→cp3 migration guide
├── language-spec/              # from docs/rough/README (6).md         — the core language
├── cli-tooling/                # from docs/rough/CLI-README (5).md     — cp3 CLI & tooling
├── standard-library/           # from docs/rough/STDLIB-README (4).md — std/* modules
└── local-docs-viewer/          # this viewer
```

`cp3ui` is a separate project and is deliberately not part of this tree.

Each topic folder holds one Markdown file per top-level section of its source
document, named `NN-slug.md` (or `00a-…`, `05a-…` for unnumbered sections that
sit between numbered ones), plus a `00-overview.md` intro.

`getting-started/` is the exception: it is **hand-written, not generated**, and
the splitter leaves it untouched. It contains the two-day on-ramp for a
developer who already knows TypeScript:

```
getting-started/
├── 00-overview.md              # the 2-day plan + how to use the guide
├── 01-day-1-the-language.md    # what's the same / what changes / what's new
├── 02-day-2-stdlib-and-cli.md  # std library + cp3 CLI you actually need
└── 03-cheatsheet.md            # TS → cp3 cheat sheet
```

## Regenerating the docs

The split is reproducible. If you edit any of the three source documents at the
repository root, regenerate the tree with:

```bash
cd docs/local-docs-viewer
npm run build:docs
```

That runs `scripts/split-docs.mjs`, which re-reads the three `*-README*.md`
files at the repo root and rewrites the three generated topic folders (it never
touches `local-docs-viewer/` or the hand-written `getting-started/` folder).
Restart the server afterwards to refresh the sidebar.

## Layout

| Path | Purpose |
|---|---|
| `server.js` | The HTTP server (routing, Markdown rendering, search, nav tree) |
| `scripts/split-docs.mjs` | Splits the three root docs into the `docs/` tree |
| `public/style.css` | Site styles: materials, typography, light + dark themes, a11y media queries |
| `public/app.js` | Theme toggle, fluid spring-driven drawer, TOC scroll-spy, search, `/` shortcut |
