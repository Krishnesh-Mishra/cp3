// esbuild-test.mjs - bundles test/suite/**/*.test.ts (plus the shared harness)
// into out/test/suite/**.test.js, preserving the folder layout. `vscode` is
// provided by the extension host and stays external. Cases are discovered by
// a recursive walk, so adding a new folder under test/suite is enough to wire
// it into the main suite.
import * as esbuild from "esbuild";
import { readdirSync, statSync } from "node:fs";
import * as path from "node:path";

function collectTestFiles(dir) {
    const found = [];
    for (const entry of readdirSync(dir)) {
        const full = path.join(dir, entry);
        if (statSync(full).isDirectory()) {
            found.push(...collectTestFiles(full));
        } else if (entry.endsWith(".test.ts")) {
            found.push(full);
        }
    }
    return found;
}

const entries = collectTestFiles("test/suite").sort();

if (entries.length === 0) {
    console.log("esbuild-test: no test files found under test/suite");
    process.exit(0);
}

await esbuild.build({
    entryPoints: entries,
    outbase: "test/suite",
    outdir: "out/test/suite",
    bundle: true,
    format: "cjs",
    platform: "node",
    target: "node20",
    external: ["vscode"],
    sourcemap: false,
    logLevel: "info",
});
