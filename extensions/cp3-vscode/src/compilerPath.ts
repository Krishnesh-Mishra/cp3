// extensions/cp3-vscode/src/compilerPath.ts
//
// Resolution of the `cp3.compilerPath` setting to a real executable.
//
// Kept free of any `vscode` import so the behavior can be smoke-tested from a
// plain Node script (scripts/resolve-smoke.mjs), outside the extension host.
//
// Contract (PKG-CONTRACTS.md section 3): the default is `"cp3c"`, and on
// Windows a bare name must also resolve as `cp3c.exe` (spawn applies PATHEXT,
// so the search here mirrors the usual suffixes explicitly instead of relying
// on the shell).

import * as fs from "node:fs";
import * as path from "node:path";

/** File names a bare command may resolve to; Windows tries the PATHEXT set. */
export function candidateFileNames(command: string): string[] {
    if (process.platform !== "win32") {
        return [command];
    }
    return [command, `${command}.exe`, `${command}.cmd`, `${command}.bat`];
}

function isFile(candidate: string): boolean {
    try {
        return fs.statSync(candidate).isFile();
    } catch {
        return false;
    }
}

/**
 * Resolves a `cp3.compilerPath` value to an absolute executable path, or
 * `undefined` when nothing matches:
 * - an absolute or path-qualified value is checked directly (plus `.exe` on
 *   Windows, so `cp3c` typed by hand next to an installed `cp3c.exe` works);
 * - a bare name is searched on `PATH` in order;
 * - if the default `cp3c` is not on `PATH`, a nearby CP3 source checkout's
 *   `build/bin/cp3c[.exe]` is used when present.
 */
export function resolveCompilerPath(
    setting: string,
    workspaceFolder?: string
): string | undefined {
    const wanted = setting.trim();
    if (wanted.length === 0) {
        return undefined;
    }
    if (path.isAbsolute(wanted) || wanted.includes("/") || wanted.includes("\\")) {
        for (const candidate of candidateFileNames(wanted)) {
            if (isFile(candidate)) {
                return path.resolve(candidate);
            }
        }
        return undefined;
    }
    const searchPath = process.env.PATH ?? "";
    for (const dir of searchPath.split(path.delimiter)) {
        if (dir.length === 0) {
            continue;
        }
        for (const name of candidateFileNames(wanted)) {
            const candidate = path.join(dir, name);
            if (isFile(candidate)) {
                return path.resolve(candidate);
            }
        }
    }
    return resolveCheckoutCompiler(wanted, workspaceFolder);
}

/**
 * A source checkout's local CMake build is a useful fallback when users open
 * CP3 from source but have not added its build/bin directory to PATH. Search
 * ancestors only after the normal PATH lookup failed, and require both the
 * project root and CLI CMake files before trusting a local build.
 */
function resolveCheckoutCompiler(
    wanted: string,
    workspaceFolder?: string
): string | undefined {
    if (!workspaceFolder || !["cp3c", "cp3c.exe"].includes(wanted.toLowerCase())) {
        return undefined;
    }
    let directory = path.resolve(workspaceFolder);
    for (let depth = 0; depth < 32; depth += 1) {
        const isCheckout =
            isFile(path.join(directory, "CMakeLists.txt")) &&
            isFile(path.join(directory, "src", "cli", "CMakeLists.txt"));
        if (isCheckout) {
            const names = process.platform === "win32"
                ? ["cp3c.exe", "cp3c"]
                : ["cp3c"];
            for (const name of names) {
                const candidate = path.join(directory, "build", "bin", name);
                if (isFile(candidate)) {
                    return path.resolve(candidate);
                }
            }
        }
        const parent = path.dirname(directory);
        if (parent === directory) {
            break;
        }
        directory = parent;
    }
    return undefined;
}
