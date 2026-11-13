// dist/extensions/cp3-vscode/scripts/brand-smoke.mjs
//
// Asserts the branding contract of the extension: the manifest is valid JSON,
// the marketplace icon points at a real PNG, and the contributed icon theme
// extends a base theme that ships with VS Code, maps `.cp3` to the logo and
// resolves its icon path relative to the theme file.
//
// The artwork checks below cover: the two SVGs that ship with the extension
// (the near-white wordmark used on dark surfaces and its near-black
// counterpart used on light surfaces - the "3" keeps the same purple
// gradient), the contributed icon theme (including its light-theme override),
// and the PNG raster the marketplace wants.
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
    // The light-theme override (a top-level association applied under the
    // `.vs` selector): it re-points the .cp3 mapping at the blackish
    // wordmark so the icon reads on a light theme. iconDefinitions may only
    // live at the top level - the light section carries associations only.
    if (data.light !== undefined) {
        const light = data.light;
        check(`the theme '${theme.id}' light override keeps iconDefinitions out`, light.iconDefinitions === undefined);
        const override = (light.fileExtensions || {}).cp3;
        check(`the theme '${theme.id}' light override re-points .cp3`, typeof override === "string" && override.length > 0, String(override));
        if (typeof override === "string" && override.length > 0) {
            check(`the light definition '${override}' exists`, (data.iconDefinitions || {})[override] !== undefined, override);
        }
    }
}

// The artwork itself.
function checkSvgArtwork(file, expectedFills) {
    const svg = readFileSync(join(root, file), "utf8");
    check(`${file} declares a square viewBox`, /viewBox="0 0 1460 1460"/.test(svg), (svg.match(/viewBox="[^"]*"/) || ["<none>"])[0]);
    check(`${file} uses no font`, !/<text[\s>]/.test(svg) && !/@font-face/.test(svg));
    check(`${file} embeds no raster image`, !/<image[\s>]/.test(svg) && !/data:image/.test(svg));
    check(`${file} references no external file`, !/(?:href|xlink:href)="(?!#)/.test(svg));
    check(`${file} keeps the purple-gradient "3"`, svg.includes('stop-color="#8F5BFF"') && svg.includes('stop-color="#7C4DF0"'));
    for (const [fill, present] of expectedFills) {
        check(`${file} ${present ? "uses" : "avoids"} ${fill}`, svg.includes(fill) === present);
    }
}

checkSvgArtwork("images/cp3-logo.svg", [["#F2F2F4", true], ["#111111", false]]);
checkSvgArtwork("images/cp3-logo-light.svg", [["#111111", true], ["#F2F2F4", false]]);

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
