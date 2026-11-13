// Diagnostics / null and undefined errors
import { diagnosticCases } from "../../harness";

diagnosticCases("Diagnostics: null and undefined errors", [
    {
        name: "throwing a number is an error because only Error objects are allowed",
        source: "import console from \"std/console\";\ntry { throw 42; } catch (e) { console.log(\"caught\"); }\n",
        expect: [{ severity: "error", code: "CP3509", line: 2, contains: "CP3 only allows Error objects" }],
    },
    {
        name: "assigning null to a boolean is an error",
        source: "const ok: boolean = null;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "assigning undefined to a string is an error",
        source: "const s: string = undefined;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "assigning null to a number is an error",
        source: "const n: number = null;\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "returning null where number is declared is an error",
        source: "function f(): number {\n  return null;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "throwing a string is an error because only Error objects are allowed",
        source: "try {\n  throw \"boom\";\n} catch (e) {\n  print(\"caught\");\n}\n",
        expect: [{ severity: "error", code: "CP3509" }],
    },
    {
        name: "throwing an object literal is an error",
        source: "try {\n  throw { code: 1 };\n} catch (e) {\n  print(\"caught\");\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "throwing a valid Error instance is not an error",
        source: "import console from \"std/console\";\ntry {\n  throw new Error(\"x\");\n} catch (e) {\n  console.log(\"caught\");\n}\n",
        expect: [],
        expectNoDiagnostics: true,
    },
    {
        name: "comparing a possibly-undefined value without a guard is an error or warning",
        source: "function f(n: number | undefined): number {\n  return n + 1;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "a null literal used as a call target is an error",
        source: "const f = null;\nf();\n",
        expect: [{ severity: "error" }],
    },
]);
