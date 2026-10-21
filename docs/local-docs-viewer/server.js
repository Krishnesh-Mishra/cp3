/**
 * cp3 local docs viewer
 * ----------------------------------------------------------------------------
 * A dependency-light (Node builtins + `marked` + `highlight.js`) web server
 * that renders the Markdown tree in `../` (the docs/ folder) as a browsable
 * site on http://localhost:3000.
 *
 *   npm install      # once
 *   npm start        # http://localhost:3000
 *
 * Routes
 *   /                      -> landing page (topic cards)
 *   /view/<rel/path.md>    -> rendered documentation page
 *   /raw/<rel/path.md>     -> the raw Markdown source
 *   /search?q=...          -> server-rendered search results (no-JS fallback)
 *   /api/search?q=...      -> JSON search results (used by as-you-type search)
 *   /assets/*              -> static assets (CSS/JS)
 */

import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { marked } from "marked";
import hljs from "highlight.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const DOCS_ROOT = path.resolve(__dirname, "..");
const PUBLIC_DIR = path.join(__dirname, "public");
const PORT = Number(process.env.PORT) || 3000;
const HOST = process.env.HOST || "127.0.0.1";

marked.setOptions({ gfm: true, breaks: false });

/* -------------------------------------------------------------- topic meta */

const LABELS = {
  "getting-started": "Start Here",
  "language-spec": "Language Spec",
  "cli-tooling": "CLI & Tooling",
  "standard-library": "Standard Library",
};

const DESCRIPTIONS = {
  "getting-started":
    "Know TypeScript already? Get productive in cp3 in about two days - what stays the same, what changes, what's new, plus the std library and CLI you actually need.",
  "language-spec":
    "Syntax, the type system, memory model, concurrency, classes, and everything else you write in a `.cp3` file.",
  "cli-tooling":
    "The `cp3` command-line tool - build/run pipeline, diagnostics and crash reporting, the package manager, `.cp3pak`, and native bindgen.",
  "standard-library":
    "The `std/*` modules with exact signatures - `console`, `JSON`, `Math`, `fs`, `network`, `Date`, `process`, `crypto` and more.",
};

function prettifyLabel(name) {
  return (
    LABELS[name] ||
    name.replace(/[-_]/g, " ").replace(/\b[a-z]/g, (c) => c.toUpperCase())
  );
}

/* ------------------------------------------------------------------ helpers */

const SKIP_DIRS = new Set(["node_modules", "local-docs-viewer", ".git"]);

const isMarkdown = (name) => name.toLowerCase().endsWith(".md");
const read = (file) => fs.readFileSync(file, "utf8");

function stripInline(s) {
  return s
    .replace(/`([^`]*)`/g, "$1")
    .replace(/\[([^\]]+)\]\([^)]*\)/g, "$1")
    .replace(/[*_~]/g, "")
    .trim();
}

function firstHeading(md, fallback) {
  const m = md.match(/^#\s+(.+?)\s*$/m);
  return m ? stripInline(m[1]) : fallback;
}

function slugify(text) {
  return (
    text
      .toLowerCase()
      .replace(/[`*_~]/g, "")
      .replace(/[^a-z0-9]+/g, "-")
      .replace(/^-+|-+$/g, "") || "section"
  );
}

function decodeEntities(s) {
  return s
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .replace(/&#x27;/g, "'")
    .replace(/&amp;/g, "&");
}

function escapeHtml(s) {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function safeResolve(relPath) {
  const cleaned = decodeURIComponent(relPath).replace(/^\/+/, "");
  const abs = path.resolve(DOCS_ROOT, cleaned);
  if (abs !== DOCS_ROOT && !abs.startsWith(DOCS_ROOT + path.sep)) return null;
  return abs;
}

/** Strip the numeric ordering prefix from a page title for display. */
function displayTitle(title) {
  const t = title.replace(/^\d+\s+/, "");
  return t || title;
}

/* --------------------------------------------------------------- nav tree */

function buildTree(dir, relBase = "") {
  const entries = fs
    .readdirSync(dir, { withFileTypes: true })
    .filter((e) => !e.name.startsWith(".") && !SKIP_DIRS.has(e.name))
    .sort((a, b) => {
      if (a.isDirectory() && !b.isDirectory()) return -1;
      if (!a.isDirectory() && b.isDirectory()) return 1;
      return a.name.localeCompare(b.name, "en", { numeric: true });
    });

  const node = {
    name: relBase ? path.basename(relBase) : "docs",
    rel: relBase,
    dirs: [],
    files: [],
  };

  for (const e of entries) {
    const rel = relBase ? `${relBase}/${e.name}` : e.name;
    if (e.isDirectory()) {
      node.dirs.push(buildTree(path.join(dir, e.name), rel));
    } else if (isMarkdown(e.name)) {
      let title = e.name.replace(/\.md$/, "");
      try {
        title = firstHeading(read(path.join(dir, e.name)), title);
      } catch {
        /* ignore */
      }
      node.files.push({ name: e.name, rel, title });
    }
  }
  return node;
}

const TREE = buildTree(DOCS_ROOT);

/** Preferred display order for the topic folders. */
const DIR_ORDER = ["getting-started", "language-spec", "cli-tooling", "standard-library"];
function orderedDirs() {
  return TREE.dirs
    .slice()
    .sort((a, b) => DIR_ORDER.indexOf(a.name) - DIR_ORDER.indexOf(b.name));
}

/** Depth-first ordered list of every page, for prev/next navigation. */
function flattenFiles() {
  const out = [];
  for (const d of orderedDirs()) {
    for (const f of d.files) {
      if (f.name === "index.md") continue;
      out.push(f);
    }
  }
  return out;
}

const ALL_FILES = flattenFiles();

/** Render the sidebar with collapsible topic groups. */
function renderSidebar(currentRel) {
  const activeDir = currentRel.includes("/") ? currentRel.split("/")[0] : null;
  const parts = [];

  parts.push(
    `<a class="nav-home${currentRel === "" || currentRel === "index.md" ? " active" : ""}" href="/">` +
      `<span class="nav-home-icon">◆</span> Home</a>`,
  );

  for (const d of orderedDirs()) {
    const open = d.name === activeDir || d.name === "getting-started";
    const pages = d.files.filter((f) => f.name !== "index.md" && !/^00-/.test(f.name));
    const overview = d.files.find((f) => /^00-/.test(f.name)) || d.files[0];
    const count = d.files.length;
    parts.push(
      `<details class="nav-group"${open ? " open" : ""}>` +
        `<summary class="nav-summary">` +
        `<span class="nav-caret" aria-hidden="true"></span>` +
        `<span class="nav-summary-label">${escapeHtml(prettifyLabel(d.name))}</span>` +
        `<span class="nav-count">${count}</span>` +
        `</summary>` +
        `<ul class="nav-list">`,
    );
    if (overview) {
      parts.push(
        `<li><a class="nav-link nav-link-overview${overview.rel === currentRel ? " active" : ""}" href="/view/${encodeURI(overview.rel)}">Overview</a></li>`,
      );
    }
    for (const f of pages) {
      const active = f.rel === currentRel ? " active" : "";
      parts.push(
        `<li><a class="nav-link${active}" href="/view/${encodeURI(f.rel)}">${escapeHtml(displayTitle(f.title))}</a></li>`,
      );
    }
    parts.push(`</ul></details>`);
  }
  return parts.join("");
}

/* ------------------------------------------------------------- page render */

function addHeadingIds(html) {
  const toc = [];
  const withIds = html.replace(/<h([1-6])>([\s\S]*?)<\/h\1>/g, (m, level, inner) => {
    const text = stripInline(decodeEntities(inner.replace(/<[^>]+>/g, "")));
    const id = slugify(text);
    if (Number(level) <= 3 && text) toc.push({ level: Number(level), text, id });
    return (
      `<h${level} id="${id}">` +
      `<a class="anchor" href="#${id}" aria-hidden="true" tabindex="-1">#</a>${inner}` +
      `</h${level}>`
    );
  });
  return { html: withIds, toc };
}

function highlightCode(html) {
  const ALIASES = { cp3: "cpp", jsx: "javascript", jsonc: "json" };
  return html.replace(
    /<pre><code(?: class="language-([\w+#-]+)")?>([\s\S]*?)<\/code><\/pre>/g,
    (m, lang, code) => {
      const source = decodeEntities(code);
      const hl = lang ? ALIASES[lang] || lang : null;
      const label = lang ? `<span class="code-lang">${escapeHtml(lang)}</span>` : "";
      let inner;
      if (hl && hljs.getLanguage(hl)) {
        try {
          inner = hljs.highlight(source, { language: hl, ignoreIllegals: true }).value;
        } catch {
          inner = escapeHtml(source);
        }
      } else {
        inner = escapeHtml(source);
      }
      const cls = `hljs${lang ? ` language-${lang}` : ""}`;
      return `<div class="code-block">${label}<pre><code class="${cls}">${inner}</code></pre></div>`;
    },
  );
}

function tocList(toc) {
  const items = toc.filter((t) => t.level >= 2);
  if (!items.length) return "";
  return (
    `<ul>` +
    items
      .map(
        (t) =>
          `<li class="toc-level-${t.level}"><a href="#${t.id}">${escapeHtml(t.text)}</a></li>`,
      )
      .join("") +
    `</ul>`
  );
}

function layout({
  title,
  currentRel,
  content,
  toc = [],
  crumb = "",
  prev = null,
  next = null,
}) {
  const sidebar = renderSidebar(currentRel);
  const tocHtml = tocList(toc);

  const prevNext = `<div class="pager">
  ${
    prev
      ? `<a class="pager-link pager-prev" href="/view/${encodeURI(prev.rel)}"><span class="pager-dir">Previous</span><span class="pager-title">${escapeHtml(displayTitle(prev.title))}</span></a>`
      : `<span class="pager-spacer"></span>`
  }
  ${
    next
      ? `<a class="pager-link pager-next" href="/view/${encodeURI(next.rel)}"><span class="pager-dir">Next</span><span class="pager-title">${escapeHtml(displayTitle(next.title))}</span></a>`
      : `<span class="pager-spacer"></span>`
  }
</div>`;

  const inlineToc = tocHtml
    ? `<details class="toc-inline"><summary>On this page</summary>${tocHtml}</details>`
    : "";

  const railToc = tocHtml
    ? `<aside class="toc-rail"><div class="toc-title">On this page</div>${tocHtml}</aside>`
    : "";

  return `<!doctype html>
<html lang="en" data-theme="light">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>${escapeHtml(displayTitle(title))} · C+++ Docs</title>
  <link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E%3Crect width='32' height='32' rx='7' fill='%230071e3'/%3E%3Ctext x='16' y='22' font-size='15' font-family='monospace' fill='white' text-anchor='middle'%3EC%3C/text%3E%3C/svg%3E" />
  <link rel="stylesheet" href="/assets/style.css" />
  <link id="hljs-light" rel="stylesheet" href="/assets/hljs-light.css" />
  <link id="hljs-dark" rel="stylesheet" href="/assets/hljs-dark.css" disabled />
</head>
<body>
  <a class="skip-link" href="#main">Skip to content</a>
  <div class="scrim" hidden></div>
  <aside class="sidebar" aria-label="Documentation navigation">
    <div class="sidebar-top">
      <a class="brand" href="/"><span class="brand-mark">C+++</span><span class="brand-sub">docs</span></a>
      <button id="nav-close" class="icon-btn nav-close" aria-label="Close navigation">×</button>
    </div>
    <div class="search" role="search">
      <input id="search-input" type="search" placeholder="Search docs…" aria-label="Search docs" autocomplete="off" />
      <kbd class="search-kbd">/</kbd>
      <div id="search-results" class="search-results" hidden></div>
    </div>
    <nav class="nav">${sidebar}</nav>
  </aside>
  <div class="main">
    <header class="topbar">
      <button id="nav-toggle" class="icon-btn nav-toggle" aria-label="Toggle navigation">☰</button>
      <nav class="breadcrumb" aria-label="Breadcrumb">${crumb || `<a href="/">Home</a>`}</nav>
      <div class="topbar-actions">
        ${
          prev
            ? `<a class="icon-btn" href="/view/${encodeURI(prev.rel)}" title="Previous: ${escapeHtml(displayTitle(prev.title))}" aria-label="Previous page">‹</a>`
            : ""
        }
        ${
          next
            ? `<a class="icon-btn" href="/view/${encodeURI(next.rel)}" title="Next: ${escapeHtml(displayTitle(next.title))}" aria-label="Next page">›</a>`
            : ""
        }
        ${
          currentRel
            ? `<a class="icon-btn" href="/raw/${encodeURI(currentRel)}" title="View raw Markdown" aria-label="View raw Markdown">↧</a>`
            : ""
        }
        <button id="theme-toggle" class="icon-btn" title="Toggle theme" aria-label="Toggle theme">◐</button>
      </div>
    </header>
    <main id="main" class="page">
      <article class="prose">
        ${inlineToc}
        ${content}
        ${prevNext}
      </article>
      ${railToc}
    </main>
  </div>
  <button id="to-top" class="to-top" aria-label="Back to top" hidden>↑</button>
  <script src="/assets/app.js"></script>
</body>
</html>`;
}

function resolvePrevNext(relPath) {
  const idx = ALL_FILES.findIndex((f) => f.rel === relPath);
  if (idx === -1) return { prev: null, next: null };
  return {
    prev: idx > 0 ? ALL_FILES[idx - 1] : null,
    next: idx < ALL_FILES.length - 1 ? ALL_FILES[idx + 1] : null,
  };
}

function crumbFor(relPath) {
  if (!relPath || relPath === "index.md") return `<a href="/">Home</a>`;
  const parts = relPath.split("/");
  const dir = parts[0];
  const file = parts[parts.length - 1];
  const title = ALL_FILES.find((f) => f.rel === relPath);
  const overview = TREE.dirs
    .find((d) => d.name === dir)
    ?.files.find((f) => /^00-/.test(f.name));
  return (
    `<a href="/">Home</a><span class="sep">/</span>` +
    (overview
      ? `<a href="/view/${encodeURI(overview.rel)}">${escapeHtml(prettifyLabel(dir))}</a>`
      : `<span>${escapeHtml(prettifyLabel(dir))}</span>`) +
    `<span class="sep">/</span><span class="current">${escapeHtml(displayTitle(title ? title.title : file))}</span>`
  );
}

function docPage(relPath) {
  const abs = safeResolve(relPath);
  if (!abs || !isMarkdown(abs) || !fs.existsSync(abs)) return null;

  const md = read(abs);
  const title = firstHeading(md, path.basename(relPath));
  const { html, toc } = addHeadingIds(marked.parse(md));
  const content = highlightCode(html);
  const { prev, next } = resolvePrevNext(relPath);
  return layout({
    title,
    currentRel: relPath,
    content,
    toc,
    crumb: crumbFor(relPath),
    prev,
    next,
  });
}

/* ------------------------------------------------------------------- home */

function homePage() {
  const guide = TREE.dirs.find((d) => d.name === "getting-started");
  const guideOverview =
    (guide && guide.files.find((f) => /^00-/.test(f.name))) || (guide && guide.files[0]) || null;
  const guidePages = guide ? guide.files.filter((f) => f.name !== "index.md" && !/^00-/.test(f.name)) : [];
  const guideHref = guideOverview
    ? `/view/${encodeURI(guideOverview.rel)}`
    : "/view/getting-started/00-overview.md";

  const startHere = guide
    ? `<section class="start-here">
    <div class="start-here-copy">
      <span class="start-here-kicker">New here? You already know TypeScript?</span>
      <h2 class="start-here-title">From TypeScript to C+++ in 2 days</h2>
      <p class="start-here-sub">Skip the internals - no memory model, no compiler internals. Just what stays the same, what changes, what's new, and the std library and CLI commands you need for everyday work.</p>
      <a class="btn btn-primary start-here-cta" href="${guideHref}">Start the 2-day guide →</a>
    </div>
    <div class="start-here-steps">
      ${guidePages
        .map((f, i) => {
          const t = displayTitle(f.title);
          const tag = /^day\s*1/i.test(t) ? "Day 1" : /^day\s*2/i.test(t) ? "Day 2" : "Reference";
          return `<a class="start-step" href="/view/${encodeURI(f.rel)}">
        <span class="start-step-tag">${tag}</span>
        <span class="start-step-title">${escapeHtml(t)}</span>
        <span class="start-step-arrow" aria-hidden="true">→</span>
      </a>`;
        })
        .join("")}
      <a class="start-step start-step-overview" href="${guideHref}">
        <span class="start-step-tag">The plan</span>
        <span class="start-step-title">Overview &amp; reading path</span>
        <span class="start-step-arrow" aria-hidden="true">→</span>
      </a>
    </div>
  </section>`
    : "";

  const cards = orderedDirs()
    .filter((d) => d.name !== "getting-started")
    .map((d) => {
      const pages = d.files.filter((f) => f.name !== "index.md");
      const overview = d.files.find((f) => /^00-/.test(f.name)) || d.files[0];
      const sections = pages.filter((f) => !/^00-/.test(f.name));
      const preview = sections.slice(0, 6);
      const more = sections.length - preview.length;
      return `<a class="home-card" href="/view/${encodeURI(overview ? overview.rel : d.dirs[0].rel)}">
  <div class="home-card-head">
    <h2 class="home-card-title">${escapeHtml(prettifyLabel(d.name))}</h2>
    <span class="home-card-badge">${sections.length} sections</span>
  </div>
  <p class="home-card-desc">${DESCRIPTIONS[d.name] || ""}</p>
  <ul class="home-card-links">
    ${preview.map((f) => `<li><span class="dot"></span>${escapeHtml(displayTitle(f.title))}</li>`).join("")}
    ${more > 0 ? `<li class="more">+ ${more} more…</li>` : ""}
  </ul>
</a>`;
    })
    .join("");

  return layout({
    title: "C+++ Documentation",
    currentRel: "",
    toc: [],
    crumb: `<span class="current">Home</span>`,
    content: `<div class="home">
  <section class="hero">
    <h1 class="hero-title">C+++ <span class="hero-lang">(.cp3)</span></h1>
    <p class="hero-sub">A JS/TS-flavored language that compiles to native code - no garbage collector, no manual memory management. Documentation for the language, its tooling, and its standard library.</p>
    <div class="hero-actions">
      <a class="btn btn-primary" href="${guideHref}">New from TypeScript? Start here</a>
      <a class="btn" href="/view/language-spec/00-overview.md">Read the language spec</a>
    </div>
  </section>
  ${startHere}
  <section class="home-grid">${cards}</section>
</div>`,
  });
}

/* ------------------------------------------------------------------ search */

function walkMarkdown(dir, out = []) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    if (e.name.startsWith(".") || SKIP_DIRS.has(e.name)) continue;
    const full = path.join(dir, e.name);
    if (e.isDirectory()) walkMarkdown(full, out);
    else if (isMarkdown(e.name)) out.push(full);
  }
  return out;
}

function searchDocs(query) {
  const q = query.trim().toLowerCase();
  if (!q) return [];
  const results = [];
  for (const file of walkMarkdown(DOCS_ROOT)) {
    const md = read(file);
    const rel = path.relative(DOCS_ROOT, file).split(path.sep).join("/");
    if (rel === "index.md") continue;
    const title = firstHeading(md, path.basename(file));
    const dir = rel.split("/")[0];
    const lines = md.split(/\r?\n/);
    let hits = 0;
    let snippet = "";
    for (let i = 0; i < lines.length; i++) {
      if (lines[i].toLowerCase().includes(q)) {
        hits++;
        if (!snippet && !/^#{1,6}\s/.test(lines[i])) {
          snippet = stripInline(lines[i]).slice(0, 180);
        }
      }
    }
    if (hits) results.push({ rel, title, dir, hits, snippet });
  }
  return results.sort((a, b) => b.hits - a.hits);
}

function searchPage(query) {
  const results = searchDocs(query);
  const body = results.length
    ? results
        .map(
          (r) => `<a class="result" href="/view/${encodeURI(r.rel)}">
  <div class="result-title">${escapeHtml(displayTitle(r.title))}</div>
  <div class="result-path">${escapeHtml(prettifyLabel(r.dir))} · ${r.hits} match${r.hits === 1 ? "" : "es"}</div>
  ${r.snippet ? `<div class="result-snippet">${escapeHtml(r.snippet)}</div>` : ""}
</a>`,
        )
        .join("")
    : `<p class="empty">No matches for <strong>${escapeHtml(query)}</strong>.</p>`;
  return layout({
    title: `Search: ${query}`,
    currentRel: "",
    crumb: `<a href="/">Home</a><span class="sep">/</span><span class="current">Search</span>`,
    content: `<h1>Search</h1><p class="muted">${results.length} page(s) matching “${escapeHtml(query)}”.</p><div class="results">${body}</div>`,
  });
}

/* ------------------------------------------------------------------- static */

const STATIC_TYPES = {
  ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".woff2": "font/woff2",
};

function serveFile(res, file, contentType) {
  try {
    const data = fs.readFileSync(file);
    res.writeHead(200, { "Content-Type": contentType, "Cache-Control": "no-cache" });
    res.end(data);
  } catch {
    res.writeHead(404, { "Content-Type": "text/plain" });
    res.end("Not found");
  }
}

function json(res, code, body) {
  res.writeHead(code, { "Content-Type": "application/json; charset=utf-8" });
  res.end(JSON.stringify(body));
}

/* ------------------------------------------------------------------- server */

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || "localhost"}`);
  const pathname = url.pathname;

  try {
    if (pathname === "/favicon.ico") {
      res.writeHead(204);
      return res.end();
    }

    if (pathname === "/" || pathname === "") {
      res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
      return res.end(homePage());
    }

    if (pathname === "/api/search") {
      return json(res, 200, { results: searchDocs(url.searchParams.get("q") || "").slice(0, 30) });
    }

    if (pathname === "/search") {
      res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
      return res.end(searchPage(url.searchParams.get("q") || ""));
    }

    if (pathname.startsWith("/assets/")) {
      const name = pathname.slice("/assets/".length);
      if (name === "hljs-light.css" || name === "hljs-dark.css") {
        const which = name === "hljs-light.css" ? "github.min.css" : "github-dark.min.css";
        return serveFile(
          res,
          path.join(__dirname, "node_modules", "highlight.js", "styles", which),
          "text/css; charset=utf-8",
        );
      }
      const file = path.join(PUBLIC_DIR, name);
      if (!file.startsWith(PUBLIC_DIR)) {
        res.writeHead(403);
        return res.end("Forbidden");
      }
      return serveFile(
        res,
        file,
        STATIC_TYPES[path.extname(name).toLowerCase()] || "application/octet-stream",
      );
    }

    if (pathname.startsWith("/raw/")) {
      const abs = safeResolve(pathname.slice("/raw/".length));
      if (!abs || !isMarkdown(abs) || !fs.existsSync(abs)) {
        res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
        return res.end("Not found");
      }
      res.writeHead(200, { "Content-Type": "text/plain; charset=utf-8" });
      return res.end(read(abs));
    }

    if (pathname.startsWith("/view/")) {
      let rel = pathname.slice("/view/".length);
      if (!rel || rel.endsWith("/")) rel += "index.md";
      const html = docPage(rel);
      if (!html) {
        res.writeHead(404, { "Content-Type": "text/html; charset=utf-8" });
        return res.end(
          layout({
            title: "Not found",
            currentRel: "",
            crumb: `<a href="/">Home</a><span class="sep">/</span><span class="current">Not found</span>`,
            content: `<h1>404 - not found</h1><p>No document at <code>${escapeHtml(rel)}</code>.</p><p><a href="/">Back to the docs home</a></p>`,
          }),
        );
      }
      res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
      return res.end(html);
    }

    res.writeHead(404, { "Content-Type": "text/plain; charset=utf-8" });
    res.end("Not found");
  } catch (err) {
    console.error(err);
    res.writeHead(500, { "Content-Type": "text/plain; charset=utf-8" });
    res.end("Internal error: " + err.message);
  }
});

server.listen(PORT, HOST, () => {
  console.log(`\n  C+++ docs viewer running at http://localhost:${PORT}\n  Serving: ${DOCS_ROOT}\n`);
});
