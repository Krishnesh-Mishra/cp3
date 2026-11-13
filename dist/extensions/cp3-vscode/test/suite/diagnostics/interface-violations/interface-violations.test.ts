// Diagnostics / interface violations
import { diagnosticCases } from "../../harness";

diagnosticCases("Diagnostics: interface violations", [
    {
        name: "a class implementing one interface misses the other is an error",
        source: "interface A {\n  a(): void;\n}\ninterface B {\n  b(): void;\n}\nclass Impl implements A, B {\n  a(): void {}\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "interface member type mismatch in a class is an error",
        source: "interface HasCount {\n  count: number;\n}\nclass Counter implements HasCount {\n  count: string = \"many\";\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "interface member with incompatible return type is an error",
        source: "interface Provider {\n  get(): number;\n}\nclass BadProvider implements Provider {\n  get(): string { return \"x\"; }\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "interface with a property declared as string rejects number in class",
        source: "interface Label {\n  text: string;\n}\nclass Widget implements Label {\n  text: number = 7;\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "method with the wrong parameter shape fails the interface",
        source: "interface Sink {\n  write(data: string): void;\n}\nclass Stdout implements Sink {\n  write(data: number): void {}\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "method missing the interface parameter is an error",
        source: "interface Pair {\n  set(pair: string): void;\n}\nclass Impl implements Pair {\n  set(): void {}\n}\n",
        expect: [{ severity: "error" }],
    },
    {
        name: "interface member satisfied by a real class member still checks",
        source: "interface HasName {\n  name: string;\n}\nclass Cat implements HasName {\n  name: string = \"miaow\";\n}\n",
        expect: [],
        expectNoDiagnostics: true,
    },
    {
        name: "interface default method can be satisfied by class member",
        source: "interface Greeting {\n  message: string;\n}\nclass Greeter implements Greeting {\n  message: string = \"hi\";\n}\n",
        expect: [],
        expectNoDiagnostics: true,
    },
    {
        name: "interface method satisfied by a class getter is accepted",
        source: "interface SizedRead {\n  size(): number;\n}\nclass Measure implements SizedRead {\n  size(): number { return 4; }\n}\n",
        expect: [],
        expectNoDiagnostics: true,
    },
    {
        name: "mismatched property kind is rejected in the implementation",
        source: "interface DataSource {\n  load(): string;\n}\nclass Broken implements DataSource {\n  load: string = \"not a function\";\n}\n",
        expect: [{ severity: "error" }],
    },
]);
