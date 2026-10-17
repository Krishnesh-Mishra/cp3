#!/usr/bin/env node
/**
 * split-docs.mjs
 * ----------------------------------------------------------------------------
 * Turns the monolithic source documents in `docs/rough/` into a browsable
 * `docs/` tree:
 *
 *   docs/
 *     index.md                     <- landing page, links to every section
 *     language-spec/               <- one folder per source document (its H1)
 *       00-overview.md             <- intro (everything before the first `##`)
 *       01-status.md               <- one file per `##` heading, in order
 *       02-why-cp3.md
 *       03-philosophy.md
 *       ...
 *     cli-tooling/
 *     standard-library/
 *
 * `cp3ui` is a separate project and is deliberately not part of this tree.
 *
 * The `getting-started/` folder is hand-written (not derived from a source
 * document) and is intentionally left untouched by this script; only the
 * landing page links to it.
 *
 * Cross references in the prose (`README.md §14`, `CLI-README.md §2`,
 * bare `§9`, ...) are rewritten into real relative links so they work both in
 * this viewer and in any ordinary Markdown renderer (GitHub, VS Code, ...).
 *
 * Run:  node scripts/split-docs.mjs
 * ----------------------------------------------------------------------------
 */

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(__dirname, "..", "..", "..");
const DOCS_ROOT = path.resolve(__dirname, "..", "..");

/** The source documents, and the folder each becomes. */
const SOURCES = [
  {
    file: "docs/rough/README (6).md",
    dir: "language-spec",
    label: "Language Spec",
    blurb:
      "The C+++ core language — syntax, the type system, memory model, concurrency, classes, and everything else you write in a `.cp3` file.",
    aliases: ["README.md", "README"],
  },
  {
    file: "docs/rough/CLI-README (5).md",
    dir: "cli-tooling",
    label: "CLI & Tooling",
    blurb:
      "The `cp3` command-line tool itself — the build/run pipeline, diagnostics and crash reporting, the package manager, the `.cp3pak` format, and native-library bindgen.",
    aliases: ["CLI-README.md"],
  },
  {
    file: "docs/rough/STDLIB-README (4).md",
    dir: "standard-library",
    label: "Standard Library",
    blurb:
      "The `std/*` modules — `console`, `JSON`, `Math`, `fs`, `network`, `Date`, `process`, `path`, `os`, `events`, `crypto`, `env`, `Map`/`Set`, `buffer` — with exact function signatures.",
    aliases: ["STDLIB-README.md"],
  },
];

/* ------------------------------------------------------------------ utils */

/** Strip a leading "3. " / "5.1 " and turn C+++/C/C++ into url-safe words. */
function cleanTitleForSlug(title) {
  return title
    .replace(/^\d+(?:\.\d+)*\.?\s+/, "")
    .replace(/C\/C\+\+/gi, "c-cpp")
    .replace(/C\+\+/gi, "cp3");
}

function slugify(text) {
  return (
    cleanTitleForSlug(text)
      .toLowerCase()
      .replace(/[`*_~?]/g, "")
      .replace(/[^a-z0-9]+/g, "-")
      .replace(/^-+|-+$/g, "")
      .slice(0, 56)
      .replace(/-+$/, "") || "section"
  );
}

const pad2 = (n) => String(n).padStart(2, "0");

/** Extract a leading section number ("3. Type system" -> 3). */
function sectionNumber(title) {
  const m = title.match(/^(\d+)\.[\s]/);
  return m ? Number(m[1]) : null;
}

/** Extract a leading subsection number ("5.1 cp3 init" -> "5.1"). */
function subsectionNumber(title) {
  const m = title.match(/^(\d+(?:\.\d+)+)\.?\s/);
  return m ? m[1] : null;
}

/**
 * Parse a document into an intro block plus an ordered list of top-level
 * (`##`) sections. Heading detection is fence-aware so `##` inside a fenced
 * code block is never treated as a real heading.
 */
function parseDocument(text) {
  const lines = text.split(/\r?\n/);
  const h1 = [];
  const sections = [];
  let inFence = false;
  let fenceChar = "";

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const fence = line.match(/^\s*(`{3,}|~{3,})/);
    if (fence) {
      if (!inFence) {
        inFence = true;
        fenceChar = fence[1][0];
      } else if (line.trim().startsWith(fenceChar.repeat(3))) {
        inFence = false;
      }
      continue;
    }
    if (inFence) continue;

    const heading = line.match(/^(#{1,6})\s+(.*?)\s*$/);
    if (!heading) continue;
    const level = heading[1].length;
    const title = heading[2].trim();

    if (level === 1) h1.push({ title, line: i });
    else if (level === 2) sections.push({ title, line: i });
  }

  // Intro = everything before the first `##` (minus the H1 line itself).
  const firstSectionLine = sections.length ? sections[0].line : lines.length;
  const h1Line = h1.length ? h1[0].line : -1;
  const introLines = lines
    .slice(h1Line + 1, firstSectionLine)
    .join("\n")
    .replace(/^\s*\n/, "")
    .replace(/\s+$/, "");

  // Attach each section's body.
  for (let s = 0; s < sections.length; s++) {
    const start = sections[s].line;
    const end = s + 1 < sections.length ? sections[s + 1].line : lines.length;
    sections[s].lines = lines.slice(start, end);
  }

  return {
    title: h1.length ? h1[0].title : "Document",
    introLines,
    sections,
  };
}

/** Demote every heading inside `lines` by one level, fence-aware. */
function demoteHeadings(lines) {
  const out = [];
  let inFence = false;
  let fenceChar = "";
  for (const line of lines) {
    const fence = line.match(/^\s*(`{3,}|~{3,})/);
    if (fence) {
      if (!inFence) {
        inFence = true;
        fenceChar = fence[1][0];
      } else if (line.trim().startsWith(fenceChar.repeat(3))) {
        inFence = false;
      }
      out.push(line);
      continue;
    }
    if (inFence) {
      out.push(line);
      continue;
    }
    const h = line.match(/^(#{1,6})(\s+.*)$/);
    if (h && h[1].length >= 2) {
      const level = Math.max(1, h[1].length - 1);
      out.push("#".repeat(level) + h[2]);
    } else {
      out.push(line);
    }
  }
  return out;
}

/* ------------------------------------------------- cross-reference linking */

/** name -> { dir, topLevel: Map<number, file>, sub: Map<string, file> } */
const DOC_MAPS = new Map();

function sectionFileFor(docKey, section) {
  const map = DOC_MAPS.get(docKey);
  if (!map || section == null) return null;
  const s = String(section);
  if (map.topLevel.has(s)) return map.topLevel.get(s);
  if (map.sub.has(s)) return map.sub.get(s);
  // "14.2" where only "14" is known -> land on section 14.
  const root = s.split(".")[0];
  if (map.topLevel.has(root)) return map.topLevel.get(root);
  return null;
}

/** Rewrite `NAME.md §N` and bare `§N` into relative Markdown links. */
function rewriteLinks(text, currentDocKey) {
  const nameToKey = new Map();
  for (const src of SOURCES) {
    for (const alias of src.aliases) {
      const base = alias.replace(/\.md$/, "");
      nameToKey.set(base, src.dir);
    }
  }
  const keyToLabel = new Map(SOURCES.map((s) => [s.dir, s.label]));
  const currentDir = currentDocKey;

  // The lookbehind stops the bare `README` alternative from matching the tail
  // of `CP3UI-README.md` (a separate project's doc that isn't in this tree).
  const crossDoc = /(?<![\w-])(`?)(README|CLI-README|STDLIB-README)\.md(`?)(?:\s*§(\d+(?:\.\d+)?))?/g;

  const lines = text.split("\n");
  const out = [];
  let inFence = false;
  let fenceChar = "";
  for (const line of lines) {
    const fence = line.match(/^\s*(`{3,}|~{3,})/);
    if (fence) {
      if (!inFence) {
        inFence = true;
        fenceChar = fence[1][0];
      } else if (line.trim().startsWith(fenceChar.repeat(3))) {
        inFence = false;
      }
      out.push(line);
      continue;
    }
    if (inFence) {
      out.push(line);
      continue;
    }

    let working = line;
    working = working.replace(
      crossDoc,
      (_m, _bt1, name, _bt2, sec) => {
        const targetDir = nameToKey.get(name);
        if (!targetDir) return _m;
        const file = sec ? sectionFileFor(targetDir, sec) : null;
        const target = file || "00-overview.md";
        const href =
          targetDir === currentDir ? target : `../${targetDir}/${target}`;
        const label = `${name}.md` + (sec ? ` §${sec}` : "");
        return `[${label}](${href})`;
      },
    );

    // Bare `§N` inside the current document. `(?![\d\]])` stops the number
    // from being matched partially (e.g. `§1` inside an already-linked
    // `[README.md §14](…)`, where `\d+` would otherwise backtrack to `1`).
    working = working.replace(
      /(?<![\[\w])§(\d+(?:\.\d+)?)(?![\d\]])/g,
      (m, sec) => {
        const file = sectionFileFor(currentDocKey, sec);
        if (!file) return m;
        return `[§${sec}](${file})`;
      },
    );

    out.push(working);
  }
  return out.join("\n");
}

/* --------------------------------------------------------------- main pass */

function build() {
  const parsed = SOURCES.map((src) => {
    const full = path.join(REPO_ROOT, src.file);
    if (!fs.existsSync(full)) {
      throw new Error(`Missing source document: ${full}`);
    }
    return { src, doc: parseDocument(fs.readFileSync(full, "utf8")) };
  });

  // Pass 1: assign every section a filename and build the lookup maps.
  for (const { src, doc } of parsed) {
    const hasNumbers = doc.sections.some((s) => sectionNumber(s.title) != null);
    let lastNum = 0;
    let letter = 0;
    const used = new Set();

    doc.sections.forEach((section, idx) => {
      let prefix;
      const num = sectionNumber(section.title);
      if (hasNumbers) {
        if (num != null) {
          prefix = pad2(num);
          lastNum = num;
          letter = 0;
        } else {
          prefix =
            lastNum === 0
              ? `00${String.fromCharCode(97 + letter++)}`
              : `${pad2(lastNum)}${String.fromCharCode(97 + letter++)}`;
        }
      } else {
        prefix = pad2(idx + 1);
      }

      let base = `${prefix}-${slugify(section.title)}`;
      let candidate = base;
      let n = 2;
      while (used.has(candidate)) candidate = `${base}-${n++}`;
      used.add(candidate);

      section.prefix = prefix;
      section.file = `${candidate}.md`;
    });

    const topLevel = new Map();
    const sub = new Map();
    for (const section of doc.sections) {
      const num = sectionNumber(section.title);
      if (num != null) topLevel.set(String(num), section.file);
      else if (/^status/i.test(section.title)) topLevel.set("status", section.file);
      // Map any `###` subsections to the file that contains them.
      for (const line of section.lines) {
        const h = line.match(/^#{3,6}\s+(.*?)\s*$/);
        if (!h) continue;
        const subNum = subsectionNumber(h[1]);
        if (subNum && !sub.has(subNum)) sub.set(subNum, section.file);
      }
    }
    DOC_MAPS.set(src.dir, { topLevel, sub, dir: src.dir });
  }

  // Pass 2: write the files.
  const manifest = [];
  for (const { src, doc } of parsed) {
    const outDir = path.join(DOCS_ROOT, src.dir);
    fs.rmSync(outDir, { recursive: true, force: true });
    fs.mkdirSync(outDir, { recursive: true });

    // Overview.
    const overviewBody = rewriteLinks(doc.introLines, src.dir);
    fs.writeFileSync(
      path.join(outDir, "00-overview.md"),
      `# ${doc.title}\n\n> ${src.blurb}\n\n${overviewBody}\n`,
      "utf8",
    );

    const entries = [
      { file: "00-overview.md", title: doc.title, isOverview: true },
    ];

    for (const section of doc.sections) {
      const body = demoteHeadings(section.lines.slice(1)).join("\n").replace(
        /^\s*\n/,
        "",
      );
      const rewritten = rewriteLinks(body, src.dir);
      const content = `# ${section.title}\n\n${rewritten}\n`.replace(/\n{4,}/g, "\n\n\n");
      fs.writeFileSync(path.join(outDir, section.file), content, "utf8");
      entries.push({ file: section.file, title: section.title });
    }

    manifest.push({ src, entries });
  }

  // Landing page.
  const indexLines = [
    "# C+++ (`.cp3`) — Documentation",
    "",
    "A browsable, section-by-section breakdown of the C+++ language, its CLI and tooling, and its standard library.",
    "",
    "> Start the local viewer with `npm start` inside `docs/local-docs-viewer/`, then open <http://localhost:3000>.",
    "",
    "## Start Here",
    "",
    "New from TypeScript? Get productive in cp3 in about two days — what stays the same, what changes, what's new, plus the std library and CLI commands you actually need.",
    "",
    "- [From TypeScript to C+++ in two days](getting-started/00-overview.md)",
    "- [Day 1 — the language](getting-started/01-day-1-the-language.md)",
    "- [Day 2 — the std library & the CLI](getting-started/02-day-2-stdlib-and-cli.md)",
    "- [TypeScript → C+++ cheat sheet](getting-started/03-cheatsheet.md)",
    "",
  ];
  for (const { src, entries } of manifest) {
    indexLines.push(`## ${src.label}`, "", src.blurb, "");
    for (const e of entries) {
      indexLines.push(`- [${e.title}](${src.dir}/${e.file})`);
    }
    indexLines.push("");
  }
  fs.writeFileSync(
    path.join(DOCS_ROOT, "index.md"),
    indexLines.join("\n"),
    "utf8",
  );

  let total = 0;
  for (const { entries } of manifest) total += entries.length;
  console.log(
    `Generated ${total} markdown files across ${manifest.length} folders in:\n  ${DOCS_ROOT}`,
  );
  for (const { src, entries } of manifest) {
    console.log(`  ${src.dir}/  (${entries.length} files)`);
  }
}

build();
