// Edge cases / pathological input vs hover, definition, references
import { robustnessCases } from "../harness";

const BROKEN: Array<[name: string, source: string]> = [
    ["unterminated object literal", "const Obj = {\n"],
    ["trailing operator at end of line", "const x = 1 +\n"],
    ["unterminated function paren", "function f(\n"],
    ["unterminated template literal", "const s = `\n"],
    ["unterminated double-quoted string", "const s = \"\n"],
    ["unterminated single-quoted string", "const s = 'abc\n"],
    ["unterminated block comment", "/* open\nconst x = 1;\n"],
    ["unterminated array literal", "const arr = [1, 2,\n"],
    ["double colon expression", "const a = b::c;\n"],
    ["class with no name", "class {\n}\n"],
    ["stray close paren", ");\n"],
    ["dangling bang operator", "const a = !;\n"],
    ["unterminated call argument", "print(compute(\n"],
    ["newline after the new keyword", "const a = new\n"],
    ["missing right-hand side of assignment", "let x = ;\n"],
    ["invalid binary digit", "const a = 0b2;\n"],
    ["unterminated block", "if (true) {\n"],
    ["incomplete import statement", "import { a } from\n"],
    ["stray standalone backslash", "const a = 1 \\\n"],
    ["dollar sign without interpolation", "const s = \"price: $\"\n"],
];

robustnessCases(
    "Edge cases: hover on pathological input",
    BROKEN.map(([name, source]) => ({
        name: `hover survives: ${name}`,
        source: `${source}let target = 1;\nconst query = __CURSOR__target;\n`,
        probe: "hover",
    })),
);

robustnessCases(
    "Edge cases: definition on pathological input",
    BROKEN.map(([name, source]) => ({
        name: `definition survives: ${name}`,
        source: `${source}let target = 1;\nconst query = __CURSOR__target;\n`,
        probe: "definition",
    })),
);

robustnessCases(
    "Edge cases: references on pathological input",
    BROKEN.map(([name, source]) => ({
        name: `references survive: ${name}`,
        source: `${source}let target = 1;\nconst query = __CURSOR__target;\n`,
        probe: "references",
    })),
);

robustnessCases(
    "Edge cases: formatting on pathological input",
    BROKEN.map(([name, source]) => ({
        name: `formatting survives: ${name}`,
        source,
        probe: "formatting",
    })),
);

robustnessCases(
    "Edge cases: code actions on pathological input",
    BROKEN.map(([name, source]) => ({
        name: `code actions survive: ${name}`,
        source: `${source}\n`,
        probe: "codeAction",
    })),
);
