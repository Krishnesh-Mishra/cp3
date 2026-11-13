// Document formatting / refuses files with lexical errors
import { formattingCases } from "../../harness";

formattingCases("Document formatting: lexical-error refusal", [
    {
        name: "an unterminated double-quoted string is refused",
        source: "const broken = \"\n",
        expectNoEdits: true,
    },
    {
        name: "an unterminated string inside an expression is refused",
        source: "const label = \"\n",
        expectNoEdits: true,
    },
    {
        name: "a stray backslash is refused",
        source: "const path = \"abc\\\n",
        expectNoEdits: true,
    },
    {
        name: "an invalid numeric literal is refused",
        source: "const x = 0xZZ;\n",
        expectNoEdits: true,
    },
    {
        name: "an invalid numeric literal deep in a function is refused",
        source: "function f(): number {\n  const broken = 0xZZ;\n  return broken;\n}\n",
        expectNoEdits: true,
    },
    {
        name: "an unterminated template literal is refused",
        source: "const s = `value\n",
        expectNoEdits: true,
    },
    {
        name: "an unterminated template inside a call is refused",
        source: "print(`never closed\n",
        expectNoEdits: true,
    },
    {
        name: "an unterminated single-quoted string is refused",
        source: "const s = 'abc\n",
        expectNoEdits: true,
    },
    {
        name: "an unmatched block comment is refused",
        source: "/* open\nconst after = 1;\n",
        expectNoEdits: true,
    },
    {
        name: "an unmatched block comment inside a class is refused",
        source: "class Machine {\n  /* open\n  value: number = 0;\n}\n",
        expectNoEdits: true,
    },
]);
