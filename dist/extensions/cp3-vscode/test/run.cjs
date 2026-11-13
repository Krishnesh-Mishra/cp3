// test/run.cjs - the mocha runner that executes inside the VS Code extension
// host. @vscode/test-electron points extensionTestsPath at this file and calls
// the exported run(); a rejection (or a thrown error) fails the launch and
// makes the CLI exit non-zero. All compiled out/test/suite/**/*.test.js files
// are picked up recursively, so new category folders join the main suite with
// no further wiring.
//
// Per-test lines are NOT relied on from stdout (VS Code's host console can
// drop or mangle large multiline reports), so the runner counts passes and
// failures itself and writes the full outcome to out/mocha-results.json.
"use strict";

const fs = require("node:fs");
const path = require("node:path");
const Mocha = require("mocha");

function collectTestFiles(dir) {
    const found = [];
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name);
        if (entry.isDirectory()) {
            found.push(...collectTestFiles(full));
        } else if (entry.name.endsWith(".test.js")) {
            found.push(full);
        }
    }
    return found;
}

module.exports.run = async function run() {
    const suiteDir = path.join(__dirname, "..", "out", "test", "suite");
    const allFiles = collectTestFiles(suiteDir).sort();
    let files = allFiles;
    // Optional comma-separated substring filter on the relative path
    // (e.g. CP3_TEST_FILTER="snippets,integration/imports-navigation").
    // Used to bisect a host crash by loading one category at a time.
    const filter = (process.env.CP3_TEST_FILTER ?? "").split(",").map((part) => part.trim()).filter(Boolean);
    if (filter.length > 0) {
        files = files.filter((file) => {
            const relative = path.relative(suiteDir, file).replaceAll("\\", "/");
            return filter.some((needle) => relative.includes(needle));
        });
    }
    // Optional sharding: CP3_TEST_SHARDS=n and CP3_TEST_SHARD_INDEX=i (0-based).
    // Round-robins the sorted file list across n shards so parallel runs can
    // merge disjoint results.
    const shards = Number(process.env.CP3_TEST_SHARDS ?? "1") || 1;
    const shardIndex = Number(process.env.CP3_TEST_SHARD_INDEX ?? "0") || 0;
    if (shards > 1) {
        files = files.filter((_, index) => index % shards === shardIndex);
    }
    console.log(`[cp3-tests] found ${files.length} test file(s) under ${suiteDir}${filter.length ? ` (filtered by ${filter.join(", ")})` : ""}${shards > 1 ? ` (shard ${shardIndex + 1}/${shards})` : ""}`);

    if (files.length === 0) {
        if (allFiles.length === 0) {
            throw new Error(`no compiled tests under ${suiteDir}; run npm run compile:test first`);
        }
        // The shard's slice (or the filter) selected nothing: write an empty
        // results file so the orchestrator can merge a clean zero, and exit.
        const shardSuffix = shards > 1 ? `-shard-${shardIndex}-of-${shards}` : "";
        const resultsPath = path.join(__dirname, "..", "out", `mocha-results${shardSuffix}.json`);
        fs.writeFileSync(resultsPath, JSON.stringify({ totalFiles: 0, passed: 0, failures: 0, elapsedMs: 0, failing: [] }, null, 2));
        return;
    }

    const mocha = new Mocha({
        ui: "tdd",
        color: false,
        reporter: "dot",
        timeout: 60_000,
    });

    for (const file of files) {
        mocha.addFile(file);
    }

    const startedAt = Date.now();
    let passed = 0;
    const failing = [];
    const runner = mocha.run(() => undefined);
    runner.on("pass", () => {
        passed += 1;
        if (passed % 100 === 0) {
            console.log(`[cp3-tests]   ...${passed} passed`);
        }
    });
    runner.on("fail", (test, error) => {
        failing.push({
            title: test.fullTitle(),
            file: test.file,
            message: error?.message,
        });
    });
    await new Promise((resolve, reject) => {
        runner.on("end", resolve);
        runner.on("error", reject);
    });
    const failures = failing.length;
    const elapsedMs = Date.now() - startedAt;

    const result = {
        totalFiles: files.length,
        passed,
        failures: failing.length,
        elapsedMs,
        failing,
        finishedAt: new Date().toISOString(),
    };
    try {
        const shardSuffix = shards > 1 ? `-shard-${shardIndex}-of-${shards}` : "";
        const resultsPath = path.join(__dirname, "..", "out", `mocha-results${shardSuffix}.json`);
        fs.writeFileSync(resultsPath, JSON.stringify(result, null, 2));
        console.log(`[cp3-tests] results written to ${resultsPath}`);
    } catch (writeError) {
        console.log(`[cp3-tests] could not write results file: ${String(writeError)}`);
    }
    console.log(`[cp3-tests] mocha finished in ${elapsedMs}ms: ${failures} failure(s)`);
    if (failures > 0) {
        throw new Error(`${failures} test(s) failed.`);
    }
};
