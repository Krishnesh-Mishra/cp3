// dist/extensions/cp3-vscode/src/intelligence/projectRoot.ts
//
// The project a file belongs to: the nearest ancestor directory with a
// cp3 project manifest (`package.json`), or the workspace folder holding the
// file when the project has no manifest. The auto-import offers modules of
// the file's own project only; a module of an unrelated project in the same
// workspace is not importable from here.

import * as path from "node:path";
import * as vscode from "vscode";

/** Whether `file` lives inside the project rooted at `root`. */
export function isInsideProject(root: string | undefined, file: string): boolean {
    if (!root) return true;
    const relative = path.relative(root, file);
    return relative !== "" && !relative.startsWith("..") && !path.isAbsolute(relative);
}

const rootCache = new Map<string, string | undefined>();

/** The project root for a file path; memoized (the lookup is a few fs.stats). */
export async function projectRootFor(file: string): Promise<string | undefined> {
    const key = path.resolve(file);
    const cached = rootCache.get(key);
    if (cached !== undefined || rootCache.has(key)) {
        return cached;
    }
    const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(file));
    const root = folder?.uri.fsPath;
    let dir = path.dirname(key);
    for (;;) {
        try {
            await vscode.workspace.fs.stat(vscode.Uri.file(path.join(dir, "package.json")));
            rootCache.set(key, dir);
            return dir;
        } catch {
            // Not a project root: keep walking up.
        }
        if (root && path.resolve(dir) === path.resolve(root)) {
            rootCache.set(key, root);
            return root;
        }
        const parent = path.dirname(dir);
        if (parent === dir) {
            rootCache.set(key, root);
            return root;
        }
        dir = parent;
    }
}

/** Posix-style specifier from `from`'s directory to `to` (with a leading `./`). */
export function relativeSpecifier(from: string, to: string): string {
    let relative = path.relative(path.dirname(from), to).split(path.sep).join("/");
    if (!relative.startsWith(".")) relative = `./${relative}`;
    return relative;
}

/** Whether the two paths point at the same file. */
export function sameFile(a: string, b: string): boolean {
    return path.resolve(a).toLowerCase() === path.resolve(b).toLowerCase();
}
