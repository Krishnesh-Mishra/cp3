// test/runTest.mjs - launches a fresh, isolated VS Code with the extension
// under development and the in-host mocha runner (test/run.cjs). A throwaway
// user-data-dir, extensions-dir and workspace folder keep the run hermetic:
// the test pins `cp3.compilerPath` (Global target) to the repository's
// release binary, and that setting vanishes with the temporary profile.
//
//   node test/runTest.mjs

import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { runTests } from "@vscode/test-electron";

const here = path.dirname(fileURLToPath(import.meta.url));
const extensionDevelopmentPath = path.resolve(here, "..");
const extensionTestsPath = path.join(extensionDevelopmentPath, "test", "run.cjs");

const userDataDir = mkdtempSync(path.join(tmpdir(), "cp3-vscode-test-userdata-"));
const extensionsDir = mkdtempSync(path.join(tmpdir(), "cp3-vscode-test-exts-"));
const workspaceDir = mkdtempSync(path.join(tmpdir(), "cp3-vscode-test-workspace-"));

try {
    await runTests({
        extensionDevelopmentPath,
        extensionTestsPath,
        launchArgs: [
            workspaceDir,
            "--disable-extensions",
            "--disable-workspace-trust",
            "--disable-telemetry",
            "--disable-gpu",
            "--start-minimized",
            "--user-data-dir", userDataDir,
            "--extensions-dir", extensionsDir,
        ],
    });
} finally {
    for (const dir of [userDataDir, extensionsDir, workspaceDir]) {
        try {
            rmSync(dir, { recursive: true, force: true });
        } catch {
            // Best effort; the OS reclaims the temp directory.
        }
    }
}
