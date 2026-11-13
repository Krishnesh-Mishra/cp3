// Edge cases / pathological input vs the completion provider
import { robustnessCases } from "../../harness";

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
    "Edge cases: completion on pathological input",
    BROKEN.map(([name, source]) => ({
        name: `completion survives: ${name}`,
        source: `${source}const query = __CURSOR__\n`,
        probe: "completion",
    })),
);
