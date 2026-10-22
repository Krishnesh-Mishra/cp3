// extensions/cp3-vscode/scripts/brand-smoke.mjs
//
// Asserts the branding contract of the extension: the manifest is valid JSON,
// the marketplace icon points at a real PNG, and the contributed icon theme
// extends a base theme that ships with VS Code, maps `.cp3` to the logo and
// resolves its icon path relative to the theme file.
//
// It also checks the artwork itself: the SVG is a 128x128 vector with no
// external references (no font, no image), and the PNG is the 128x128 raster
// of it.
//
//   node scripts/brand-smoke.mjs

import { existsSync, readFileSync, statSync } from "node:fs";
import { dirname, join, normalize, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");

let failures = 0;
function check(label, ok, detail = "") {
    if (!ok) {
        failures += 1;
    }
    console.log(`${ok ? "ok  " : "FAIL"} ${label}${detail ? `\n     ${detail}` : ""}`);
}

function readJson(path) {
    const file = join(root, path);
    if (!existsSync(file)) {
        return undefined;
    }
    try {
        return JSON.parse(readFileSync(file, "utf8"));
    } catch (error) {
        return undefined;
    }
}

const manifest = readJson("package.json");
check("package.json is valid JSON", manifest !== undefined);
if (manifest === undefined) {
    console.log("brand-smoke: cannot continue without a valid manifest");
    process.exit(1);
}

// The marketplace icon: a PNG that exists next to the manifest.
const icon = manifest.icon;
check("the manifest contributes an icon", typeof icon === "string" && icon.length > 0, String(icon));
if (typeof icon === "string" && icon.length > 0) {
    const iconPath = join(root, icon);
    check("the icon file exists", existsSync(iconPath), iconPath);
    check("the icon is a PNG", iconPath.endsWith(".png"), iconPath);
    check("the icon is not empty", existsSync(iconPath) && statSync(iconPath).size > 0);
}

// The contributed icon theme.
const themes = (manifest.contributes && manifest.contributes.iconThemes) || [];
check("the manifest contributes an icon theme", themes.length > 0, `${themes.length} theme(s)`);
for (const theme of themes) {
    const themePath = join(root, theme.path);
    check(`the theme file of '${theme.id}' exists`, existsSync(themePath), themePath);
    const data = readJson(theme.path);
    if (data === undefined) {
        check(`the theme file of '${theme.id}' is valid JSON`, false, theme.path);
        continue;
    }
    check(`the theme '${theme.id}' keeps the contributed id`, data.id === theme.id, `${data.id}`);
    check(
        `the theme '${theme.id}' extends a built-in base theme`,
        typeof data.extends === "string" && data.extends.length > 0,
        String(data.extends)
    );
    check(
        `the theme '${theme.id}' maps the .cp3 extension`,
        (data.fileExtensions || {}).cp3 !== undefined,
        JSON.stringify(data.fileExtensions || {})
    );
    for (const [name, definition] of Object.entries(data.iconDefinitions || {})) {
        const iconPath = normalize(join(dirname(themePath), definition.iconPath));
        check(`the icon '${name}' of '${theme.id}' exists`, existsSync(iconPath), iconPath);
    }
}

// The artwork itself.
const svg = readFileSync(join(root, "images", "cp3-logo.svg"), "utf8");
check("the SVG declares a 128x128 viewBox", svg.includes('viewBox="0 0 128 128"'));
check("the SVG uses no font", !/<text[\s>]/.test(svg) && !/@font-face/.test(svg));
check("the SVG embeds no raster image", !/<image[\s>]/.test(svg) && !/data:image/.test(svg));
check("the SVG references no external file", !/(?:href|xlink:href)="(?!#)/.test(svg));

const png = readFileSync(join(root, "images", "cp3-logo.png"));
check("the PNG is a PNG file", png.subarray(1, 4).toString("ascii") === "PNG");
const width = png.readUInt32BE(16);
const height = png.readUInt32BE(20);
check("the PNG is 128x128", width === 128 && height === 128, `${width}x${height}`);

console.log(
    failures === 0
        ? "brand-smoke: all checks passed"
        : `brand-smoke: ${failures} failure(s)`
);
process.exit(failures === 0 ? 0 : 1);
