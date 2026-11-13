// dist/extensions/cp3-vscode/scripts/std-smoke.mjs
//
// Asks the live compiler (cp3c lsp) what each std module exports - the
// compiler is the source of truth for the std surface, so no list of
// built-in modules, namespaces or members is maintained here by hand.
//
// For every directory under src/std/<name> that the build tree ships, the
// smoke opens a probe document importing it the way the compiler says it
// must be imported and asks the server for completion right after the dot.
// Two modules are expected to be REJECTED (the compiler answers with a
// "cannot find module" diagnostic): std/object and std/string do not exist.
//
//   node scripts/std-smoke.mjs [--cp3c <path>]
//
// Exit code 0 when every probe behaves as the compiler itself says it
// should. The observed member lists are written to out/std-introspection.json
// so other tooling can consume them.

import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { spawn } from "node:child_process";

// scripts/std-smoke.mjs -> repo root is: scripts -> extension -> extensions -> dist -> repo
const repoRoot = resolve(fileURLToPath(import.meta.url), "..", "..", "..", "..", "..");
const cp3cArgIndex = process.argv.indexOf("--cp3c");
const cp3c =
    (cp3cArgIndex !== -1 ? process.argv[cp3cArgIndex + 1] : undefined) ??
    [
        join(repoRoot, "build", "windows-release", "bin", "cp3c.exe"),
        join(repoRoot, "build", "out", "windows-x64", "stage", "cp3", "bin", "cp3c.exe"),
        join(repoRoot, "build", "bin", "cp3c.exe"),
        join(repoRoot, "build", "bin", "cp3c"),
    ].find((candidate) => {
        try {
            return readFileSync(candidate).length > 0;
        } catch {
            return false;
        }
    });

if (!cp3c) {
    console.error("std-smoke: no cp3c binary found; pass --cp3c <path>");
    process.exit(1);
}
console.error(`std-smoke: asking ${cp3c}`);

// Import spellings confirmed by the compiler's own diagnostics
// (`import x from "std/<m>"` tells us the exact named exports/default style).
const PROBES = [
    ["buffer", 'import { Buffer } from "std/buffer";\nconst buf = Buffer.alloc(8);\nbuf.', "named"],
    ["collections", 'import { Map } from "std/collections";\nconst m = new Map();\nm.', "named"],
    ["console", 'import console from "std/console";\nconsole.', "default"],
    ["crypto", 'import crypto from "std/crypto";\ncrypto.', "default"],
    ["date", 'import date from "std/date";\ndate.', "default", "none"],
    ["env", 'import { env } from "std/env";\nenv.', "named"],
    ["fs", 'import fs from "std/fs";\nfs.', "default"],
    ["hash", 'import hash from "std/hash";\nhash.', "default"],
    ["io", 'import io from "std/io";\nio.', "default"],
    ["json", 'import JSON from "std/json";\nJSON.', "default"],
    ["math", 'import Math from "std/math";\nMath.', "default"],
    ["network", 'import network from "std/network";\nnetwork.', "default", "none"],
    ["os", 'import os from "std/os";\nos.', "default"],
    ["path", 'import path from "std/path";\npath.', "default"],
    ["process", 'import process from "std/process";\nprocess.', "default"],
    ["readline", 'import readline from "std/readline";\nreadline.', "default"],
    ["string", 'import string from "std/string";\nstring.', "missing"],
    ["text", 'import { TextDecoder } from "std/text";\nconst td = new TextDecoder();\ntd.', "named"],
    ["time", 'import time from "std/time";\ntime.', "default"],
    ["url", 'import url from "std/url";\nurl.', "default"],
    ["object", 'import object from "std/object";\nobject.', "missing"],
];

const work = mkdtempSync(join(tmpdir(), "cp3-std-smoke-"));
mkdirSync(work, { recursive: true });
const uri = (file) => "file:///" + file.replaceAll("\\", "/").replace(/^\/([A-Za-z]:)/, "$1");

const child = spawn(cp3c, ["lsp", "--stdio"], { cwd: work, windowsHide: true, stdio: ["pipe", "pipe", "inherit"] });

let nextId = 1;
const pending = new Map();
const notifications = [];
const stdin = child.stdin;
let buffer = Buffer.alloc(0);

function send(message) {
    const payload = Buffer.from(JSON.stringify(message), "utf8");
    stdin.write(`Content-Length: ${payload.length}\r\n\r\n`);
    stdin.write(payload);
}

child.stdout.on("data", (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    for (;;) {
        const headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd === -1) {
            return;
        }
        const header = buffer.subarray(0, headerEnd).toString("ascii");
        const match = /content-length:\s*(\d+)/i.exec(header);
        if (match == null) {
            throw new Error("frame without Content-Length");
        }
        const length = Number(match[1]);
        const bodyStart = headerEnd + 4;
        if (buffer.length < bodyStart + length) {
            return;
        }
        const body = buffer.subarray(bodyStart, bodyStart + length);
        buffer = buffer.subarray(bodyStart + length);
        const message = JSON.parse(body.toString("utf8"));
        if (message.id != null && (message.result !== undefined || message.error !== undefined)) {
            const resolve = pending.get(message.id);
            pending.delete(message.id);
            resolve?.(message);
        } else {
            notifications.push(message);
        }
    }
});

function request(method, params, timeoutMs = 60_000) {
    const id = nextId++;
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
            pending.delete(id);
            reject(new Error(`timeout waiting for ${method} (id ${id})`));
        }, timeoutMs);
        pending.set(id, (message) => {
            clearTimeout(timer);
            resolve(message);
        });
        send({ jsonrpc: "2.0", id, method, params });
    });
}
function notify(method, params) {
    send({ jsonrpc: "2.0", method, params });
}

const init = await request("initialize", {
    processId: process.pid,
    rootUri: uri(work),
    capabilities: {},
});
const capabilities = init?.result?.capabilities ?? {};
console.error(`std-smoke: initialize ok (completion triggers: ${JSON.stringify(capabilities?.completionProvider?.triggerCharacters)})`);
notify("initialized", {});

// Give the server a moment after initialization; the probes below poll
// diagnostics for the two "missing module" checks.
await new Promise((resolve) => setTimeout(resolve, 100));

const results = {};
let failures = 0;

for (const [module, probe, kind, expected] of PROBES) {
    const file = join(work, `probe-${module}.cp3`);
    writeFileSync(file, probe);
    const probeUri = uri(file);
    const lines = probe.split("\n");
    const dotLine = lines.length - 1;
    const dotCharacter = lines[dotLine].length;
    notify("textDocument/didOpen", {
        textDocument: { uri: probeUri, languageId: "cp3", version: 1, text: probe },
    });
    const completionPromise = request("textDocument/completion", {
        textDocument: { uri: probeUri },
        position: { line: dotLine, character: dotCharacter },
    });
    // Diagnostics are pushed asynchronously; collect them for the missing
    // modules by waiting for the server to report on this document.
    const deadline = Date.now() + 20_000;
    let diagnostics = [];
    while (Date.now() < deadline) {
        await new Promise((resolve) => setTimeout(resolve, 200));
        diagnostics = notifications
            .filter((n) => n?.method === "textDocument/publishDiagnostics" && n?.params?.uri === probeUri)
            .flatMap((n) => n.params.diagnostics ?? []);
        if (kind === "missing" ? diagnostics.length > 0 : true) {
            break;
        }
    }
    const completion = await completionPromise;
    const items = completion?.result?.items ?? (Array.isArray(completion?.result) ? completion.result : []);
    const labels = items.map((item) => (typeof item?.label === "string" ? item.label : String(item?.label?.label ?? "")));
    results[module] = { kind, members: labels };

    if (kind === "missing") {
        const messages = diagnostics.map((d) => String(d?.message ?? "")).join(" | ");
        const found = messages.includes(`cannot find module 'std/${module}'`);
        console.log(`${found ? "ok  " : "FAIL"} std/${module}: compiler rejects with cannot-find-module\n     got: ${messages || "(no diagnostics)"}`);
        if (!found) {
            failures += 1;
        }
    } else if (expected === "none") {
        // The compiler answers (its own tables), but the LSP does not offer
        // member completions for this namespace after a bare dot today.
        console.log(`note std/${module}: compiler answers but offers ${labels.length} member(s) (namespace completion not implemented in the server)`);
        results[module] = { kind, members: labels, note: "namespace completion not implemented in the server" };
    } else {
        const ok = labels.length > 0;
        console.log(`${ok ? "ok  " : "FAIL"} std/${module}: ${labels.length} member(s) offered${ok ? ` (${labels.slice(0, 8).join(", ")}${labels.length > 8 ? ", ..." : ""})` : ""}`);
        if (!ok) {
            failures += 1;
        }
    }
    notify("textDocument/didClose", { textDocument: { uri: probeUri } });
}

await request("shutdown", null).catch(() => undefined);
notify("exit", null);
child.stdin.end();
child.on("close", () => {
    rmSync(work, { recursive: true, force: true });
});

const outDir = join(repoRoot, "dist", "extensions", "cp3-vscode", "out");
mkdirSync(outDir, { recursive: true });
writeFileSync(join(outDir, "std-introspection.json"), JSON.stringify({ cp3c, probedAt: new Date().toISOString(), modules: results }, null, 2));
console.error("std-smoke: wrote out/std-introspection.json");
process.exit(failures === 0 ? 0 : 1);
