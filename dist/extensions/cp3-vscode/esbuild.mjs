// esbuild.mjs - bundles src/extension.ts (plus its node_modules dependencies)
// into the single self-contained dist/extension.js the .vsix ships.
// `vscode` is provided by the extension host and stays external.
import * as esbuild from "esbuild";

const production = process.argv.includes("--production");
const watch = process.argv.includes("--watch");

const context = await esbuild.context({
    entryPoints: ["src/extension.ts"],
    outfile: "dist/extension.js",
    bundle: true,
    format: "cjs",
    platform: "node",
    target: "node20",
    external: ["vscode"],
    minify: production,
    sourcemap: false,
    logLevel: "info",
});

if (watch) {
    await context.watch();
    console.log("esbuild: watching src/extension.ts");
} else {
    await context.rebuild();
    await context.dispose();
}
