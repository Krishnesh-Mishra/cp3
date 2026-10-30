// extensions/cp3-vscode/scripts/resolve-smoke.mjs
//
// Runs the real src/compilerPath.ts resolution logic outside VS Code and
// asserts the contract: bare `cp3c` resolves through PATH including the
// Windows `cp3c.exe` suffix, path-qualified values resolve directly, and
// nothing matching yields undefined (the graceful "server not found" path).
//
//   node scripts/resolve-smoke.mjs

import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { tmpdir } from "node:os";
import { join } from "node:path";

import * as esbuild from "esbuild";

const require = createRequire(import.meta.url);
const work = mkdtempSync(join(tmpdir(), "cp3-resolve-"));
const bundle = join(work, "compilerPath.cjs");

let failures = 0;
function check(label, actual, expected) {
    const ok = actual === expected;
    if (!ok) {
        failures += 1;
    }
    console.log(`${ok ? "ok  " : "FAIL"} ${label}\n     expected: ${expected}\n     actual:   ${actual}`);
}

try {
    await esbuild.build({
        entryPoints: ["src/compilerPath.ts"],
        outfile: bundle,
        bundle: true,
        platform: "node",
        format: "cjs",
        logLevel: "silent",
    });
    const { candidateFileNames, resolveCompilerPath } = require(bundle);

    const binExe = join(work, "bin-exe");
    mkdirSync(binExe);
    const fakeExe = join(binExe, "cp3c.exe");
    writeFileSync(fakeExe, "");
    const binBoth = join(work, "bin-both");
    mkdirSync(binBoth);
    const fakeExact = join(binBoth, "cp3c");
    writeFileSync(fakeExact, "");
    writeFileSync(join(binBoth, "cp3c.exe"), "");

    const originalPath = process.env.PATH;
    try {
        process.env.PATH = binExe;
        check(
            "bare 'cp3c' on PATH finds cp3c.exe",
            resolveCompilerPath("cp3c"),
            fakeExe
        );
        check(
            "bare 'cp3c.exe' on PATH finds cp3c.exe",
            resolveCompilerPath("cp3c.exe"),
            fakeExe
        );
        check(
            "absolute path without extension finds cp3c.exe",
            resolveCompilerPath(join(binExe, "cp3c")),
            fakeExe
        );
        check(
            "absolute path to the file itself resolves",
            resolveCompilerPath(fakeExe),
            fakeExe
        );

        process.env.PATH = binBoth;
        check(
            "an exact 'cp3c' file wins over cp3c.exe",
            resolveCompilerPath("cp3c"),
            fakeExact
        );

        process.env.PATH = join(work, "empty");
        check("missing binary yields undefined", resolveCompilerPath("cp3c"), undefined);
        check("empty setting yields undefined", resolveCompilerPath("   "), undefined);

        const checkout = join(work, "checkout");
        const checkoutBin = join(checkout, "build", "bin");
        const checkoutCli = join(checkout, "src", "cli");
        const nestedWorkspace = join(checkout, "examples", "servers", "healthz");
        mkdirSync(checkoutBin, { recursive: true });
        mkdirSync(checkoutCli, { recursive: true });
        mkdirSync(nestedWorkspace, { recursive: true });
        writeFileSync(join(checkout, "CMakeLists.txt"), "");
        writeFileSync(join(checkoutCli, "CMakeLists.txt"), "");
        const localName = process.platform === "win32" ? "cp3c.exe" : "cp3c";
        const localCompiler = join(checkoutBin, localName);
        writeFileSync(localCompiler, "");
        check(
            "default cp3c finds a nearby source-checkout build",
            resolveCompilerPath("cp3c", nestedWorkspace),
            localCompiler
        );
        check(
            "an explicit missing compiler path does not use checkout fallback",
            resolveCompilerPath(join(checkout, "missing-cp3c"), nestedWorkspace),
            undefined
        );
        check(
            "nonexistent absolute path yields undefined",
            resolveCompilerPath(join(binExe, "nope")),
            undefined
        );
    } finally {
        process.env.PATH = originalPath;
    }

    if (process.platform === "win32") {
        check(
            "candidate list includes .exe on Windows",
            candidateFileNames("cp3c").join(","),
            "cp3c,cp3c.exe,cp3c.cmd,cp3c.bat"
        );
    } else {
        check("candidate list is bare on non-Windows", candidateFileNames("cp3c").join(","), "cp3c");
    }
} finally {
    rmSync(work, { recursive: true, force: true });
}

console.log(failures === 0 ? "resolve-smoke: all checks passed" : `resolve-smoke: ${failures} failure(s)`);
process.exit(failures === 0 ? 0 : 1);
