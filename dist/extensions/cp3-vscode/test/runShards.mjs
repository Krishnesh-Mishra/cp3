// test/runShards.mjs - runs the extension-host test suite in N parallel
// shards. Each shard is a full isolated VS Code instance with its own
// extension host, temporary workspace and language-server process, so the
// shards can proceed independently and safely execute all ~1,400 cases.
//
//   CP3_TEST_SHARDS=5 npm test             # default: min(5, cpu count)
//   CP3_TEST_SHARDS=4 node test/runShards.mjs
//
// Every shard launches test/runTest.mjs with CP3_TEST_SHARDS and
// CP3_TEST_SHARD_INDEX set; run.cjs then picks a disjoint round-robin slice
// of the compiled test files. Results are merged from out/mocha-results-*.json.
import { spawn } from "node:child_process";
import { cpus, tmpdir } from "node:os";
import { existsSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const extensionRoot = path.resolve(here, "..");
const outDir = path.join(extensionRoot, "out");

const shards = Math.max(1, Math.min(16, Number(process.env.CP3_TEST_SHARDS ?? "0") || Math.min(5, cpus().length)));
if (shards === 1) {
    const code = await launch(0, 1);
    process.exit(code);
}

console.error(`[cp3-tests] launching ${shards} parallel shard(s) (${cpus().length} CPUs)`);

// Drop stale shard results from a previous run so the merge is clean.
for (let index = 0; index < shards; index += 1) {
    rmSync(path.join(outDir, `mocha-results-shard-${index}-of-${shards}.json`), { force: true });
}

const exitCodes = await Promise.all(
    Array.from({ length: shards }, (_, index) => launch(index, shards)),
);

// Merge the shard results.
const merged = { totalFiles: 0, passed: 0, failures: 0, elapsedMs: 0, failing: [] };
let foundAny = false;
for (let index = 0; index < shards; index += 1) {
    const resultsPath = path.join(outDir, `mocha-results-shard-${index}-of-${shards}.json`);
    if (!existsSync(resultsPath)) {
        console.error(`[cp3-tests] shard ${index + 1}/${shards} wrote no results file`);
        continue;
    }
    const shard = JSON.parse(readFileSync(resultsPath, "utf8"));
    merged.totalFiles += shard.totalFiles;
    merged.passed += shard.passed;
    merged.failures += shard.failures;
    merged.elapsedMs = Math.max(merged.elapsedMs, shard.elapsedMs);
    merged.failing.push(...shard.failing);
    foundAny = true;
}
merged.elapsedSec = Math.round(merged.elapsedMs / 1000);
writeFileSync(path.join(outDir, "mocha-results.json"), JSON.stringify(merged, null, 2));
console.error(`[cp3-tests] merged: ${merged.passed} passed, ${merged.failures} failed across ${merged.totalFiles} file(s) in ${merged.elapsedSec}s`);

if (!foundAny || exitCodes.some((code) => code !== 0) || merged.failures > 0) {
    process.exitCode = 1;
}

async function launch(index, total) {
    return new Promise((resolve, reject) => {
        const logs = {
            detached: false,
            env: {
                ...process.env,
                CP3_TEST_SHARDS: String(total),
                CP3_TEST_SHARD_INDEX: String(index),
            },
        };
        const child = spawn(process.execPath, [path.join(here, "runTest.mjs")], { ...logs, stdio: "inherit" });
        child.on("error", reject);
        child.on("close", (code) => resolve(code ?? 1));
    });
}
