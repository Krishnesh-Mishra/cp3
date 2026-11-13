#!/usr/bin/env python3
"""Generates the giant-repo-dev-reload fixture deterministically.

The fixture is a large, deeply interlinked module graph - the shape the
v0.2.5 fast-dev-loop work (per-module TUs + incremental frontend) exists
for. One "Hello, world!" string lives in lib/base/b0; the program prints
it through a four-level import chain, so an edit to that one leaf forces
the build to re-resolve the graph and recompile everything downstream.

Layers (128 / 128 / 64 / entry):

    base/b0..b127.cp3   leaf arithmetic (b0 also exports greet())
    mid/m0..m127.cp3    each imports two bases; m0 re-exports `greet`
    app/a0..a63.cp3     each imports two mids; a0 re-exports `greet`
    src/index.cp3       prints greet(); imports everything else too

Re-running this script overwrites the fixture with identical bytes (no
timestamps, no randomness). Run:

    python gen_fixture.py
"""
from __future__ import annotations

from pathlib import Path

HERE = Path(__file__).resolve().parent

N_BASE = 128
N_MID = 128
N_APP = 64


def base_body(i: int) -> str:
    return (
        f"export function b{i}(x: number): number {{\n"
        f"    let acc = x + {i};\n"
        f"    acc = (acc * 31 + {i * 7}) % 1000033;\n"
        f"    acc = (acc * 131 + x * 17 + {i}) % 1000199;\n"
        f"    return acc;\n"
        f"}}\n"
    )


def base_cp3(i: int) -> str:
    body = base_body(i)
    if i == 0:
        body += 'export function greet(): string {\n    return "Hello, world!";\n}\n'
    return body


def mid_cp3(i: int) -> str:
    a, b = (3 * i) % N_BASE, (3 * i + 40) % N_BASE
    out = (
        f'import {{ b{a} }} from "../base/b{a}.cp3";\n'
        f'import {{ b{b} }} from "../base/b{b}.cp3";\n'
        f"export function m{i}(x: number): number {{\n"
        f"    return b{a}(x) + b{b}(x + 1);\n"
        f"}}\n"
    )
    if i == 0:
        out += 'export { greet } from "../base/b0.cp3";\n'
    return out


def app_cp3(i: int) -> str:
    a, b = (5 * i) % N_MID, (5 * i + 77) % N_MID
    out = (
        f'import {{ m{a} }} from "../mid/m{a}.cp3";\n'
        f'import {{ m{b} }} from "../mid/m{b}.cp3";\n'
        f"export function a{i}(x: number): number {{\n"
        f"    return m{a}(x) + m{b}(x + 2);\n"
        f"}}\n"
    )
    if i == 0:
        out += 'export { greet } from "../mid/m0.cp3";\n'
    return out


def cp3_entry() -> str:
    imports = ['import console from "std/console";']
    imports.append('import { greet, a0 } from "./lib/app/a0.cp3";')
    for i in range(1, N_APP):
        imports.append(f'import {{ a{i} }} from "./lib/app/a{i}.cp3";')
    checksum = " + ".join(f"a{i}({i})" for i in range(N_APP))
    return (
        "\n".join(imports)
        + "\n\nconst checksum = "
        + checksum
        + ";\nconsole.log(greet());\n"
        + "void checksum;\n"
    )


def base_cpp(i: int) -> str:
    body = (
        f"#ifndef GIANT_BASE_B{i}_HPP\n"
        f"#define GIANT_BASE_B{i}_HPP\n\n"
        f"inline int b{i}(int x) {{\n"
        f"    int acc = x + {i};\n"
        f"    acc = (acc * 31 + {i * 7}) % 1000033;\n"
        f"    acc = (acc * 131 + x * 17 + {i}) % 1000199;\n"
        f"    return acc;\n"
        f"}}\n\n"
    )
    if i == 0:
        body += 'inline const char* greet() { return "Hello, world!"; }\n\n'
    body += f"#endif  // GIANT_BASE_B{i}_HPP\n"
    return body


def mid_cpp(i: int) -> str:
    a, b = (3 * i) % N_BASE, (3 * i + 40) % N_BASE
    return (
        f"#ifndef GIANT_MID_M{i}_HPP\n"
        f"#define GIANT_MID_M{i}_HPP\n\n"
        f'#include "../base/b{a}.hpp"\n'
        f'#include "../base/b{b}.hpp"\n\n'
        f"inline int m{i}(int x) {{\n"
        f"    return b{a}(x) + b{b}(x + 1);\n"
        f"}}\n\n"
        f"#endif  // GIANT_MID_M{i}_HPP\n"
    )


def app_cpp(i: int) -> str:
    a, b = (5 * i) % N_MID, (5 * i + 77) % N_MID
    return (
        f"#ifndef GIANT_APP_A{i}_HPP\n"
        f"#define GIANT_APP_A{i}_HPP\n\n"
        f'#include "../mid/m{a}.hpp"\n'
        f'#include "../mid/m{b}.hpp"\n\n'
        f"inline int a{i}(int x) {{\n"
        f"    return m{a}(x) + m{b}(x + 2);\n"
        f"}}\n\n"
        f"#endif  // GIANT_APP_A{i}_HPP\n"
    )


def cpp_main() -> str:
    lines = ['#include "lib/app/a0.hpp"']
    for i in range(1, N_APP):
        lines.append(f'#include "lib/app/a{i}.hpp"')
    checksum = " + ".join(f"a{i}({i})" for i in range(N_APP))
    return (
        "\n".join(lines)
        + "\n#include <iostream>\n\n"
        + "int main() {\n"
        + f"    int checksum = {checksum};\n"
        + '    (void)checksum;\n'
        + "    std::cout << greet() << std::endl;\n"
        + "    return 0;\n"
        + "}\n"
    )


def base_rust(i: int) -> str:
    body = (
        f"pub fn b{i}(x: i64) -> i64 {{\n"
        f"    let mut acc = x + {i};\n"
        f"    acc = (acc * 31 + {i * 7}) % 1000033;\n"
        f"    acc = (acc * 131 + x * 17 + {i}) % 1000199;\n"
        f"    acc\n"
        f"}}\n"
    )
    if i == 0:
        body += 'pub fn greet() -> &\'static str { "Hello, world!" }\n'
    return body


def mid_rust(i: int) -> str:
    a, b = (3 * i) % N_BASE, (3 * i + 40) % N_BASE
    return (
        f"use crate::base::b{a}::b{a};\n"
        f"use crate::base::b{b}::b{b};\n\n"
        f"pub fn m{i}(x: i64) -> i64 {{\n"
        f"    b{a}(x) + b{b}(x + 1)\n"
        f"}}\n\n"
        + ('pub use crate::base::b0::greet;\n' if i == 0 else "")
    )


def app_rust(i: int) -> str:
    a, b = (5 * i) % N_MID, (5 * i + 77) % N_MID
    return (
        f"use crate::mid::m{a}::m{a};\n"
        f"use crate::mid::m{b}::m{b};\n\n"
        f"pub fn a{i}(x: i64) -> i64 {{\n"
        f"    m{a}(x) + m{b}(x + 2)\n"
        f"}}\n\n"
        + ('pub use crate::mid::m0::greet;\n' if i == 0 else "")
    )


def rust_main() -> str:
    mods = (
        "pub mod base {\n"
        + "".join(f"    pub mod b{i};\n" for i in range(N_BASE))
        + "}\n\n"
        "pub mod mid {\n"
        + "".join(f"    pub mod m{i};\n" for i in range(N_MID))
        + "}\n\n"
        "pub mod app {\n"
        + "".join(f"    pub mod a{i};\n" for i in range(N_APP))
        + "}\n\n"
    )
    checksum = " + ".join(f"app::a{i}::a{i}({i})" for i in range(N_APP))
    return (
        mods
        + "fn main() {\n"
        + f"    let checksum: i64 = {checksum};\n"
        + "    let _ = checksum;\n"
        + '    println!("{}", app::a0::greet());\n'
        + "}\n"
    )


def base_go(i: int) -> str:
    body = (
        f"package base\n\n"
        f"func B{i}(x int) int {{\n"
        f"    acc := x + {i}\n"
        f"    acc = (acc * 31 + {i * 7}) % 1000033\n"
        f"    acc = (acc * 131 + x * 17 + {i}) % 1000199\n"
        f"    return acc\n"
        f"}}\n\n"
    )
    if i == 0:
        body += 'func Greet() string { return "Hello, world!" }\n'
    return body


def mid_go(i: int) -> str:
    a, b = (3 * i) % N_BASE, (3 * i + 40) % N_BASE
    return (
        f"package mid\n\n"
        f'import "giant/src/lib/base"\n\n'
        f"func M{i}(x int) int {{\n"
        f"    return base.B{a}(x) + base.B{b}(x + 1)\n"
        f"}}\n\n"
        + ('func Greet() string { return base.Greet() }\n' if i == 0 else "")
    )


def app_go(i: int) -> str:
    a, b = (5 * i) % N_MID, (5 * i + 77) % N_MID
    return (
        f"package app\n\n"
        f'import "giant/src/lib/mid"\n\n'
        f"func A{i}(x int) int {{\n"
        f"    return mid.M{a}(x) + mid.M{b}(x + 2)\n"
        f"}}\n\n"
        + ('func Greet() string { return mid.Greet() }\n' if i == 0 else "")
    )


def go_main() -> str:
    checksum = " + ".join(f"app.A{i}({i})" for i in range(N_APP))
    return (
        "package main\n\n"
        'import (\n'
        '    "fmt"\n'
        '    "giant/src/lib/app"\n'
        ")\n\n"
        "func main() {\n"
        f"    checksum := {checksum}\n"
        "    _ = checksum\n"
        '    fmt.Println(app.Greet())\n'
        "}\n"
    )


def base_java(i: int) -> str:
    body = (
        f"package giant.lib.base;\n\n"
        f"public class B{i} {{\n"
        f"    public static int value(int x) {{\n"
        f"        int acc = x + {i};\n"
        f"        acc = (acc * 31 + {i * 7}) % 1000033;\n"
        f"        acc = (acc * 131 + x * 17 + {i}) % 1000199;\n"
        f"        return acc;\n"
        f"    }}\n"
    )
    if i == 0:
        body += (
            '    public static final String GREET = "Hello, world!";\n'
        )
    body += "}\n"
    return body


def mid_java(i: int) -> str:
    a, b = (3 * i) % N_BASE, (3 * i + 40) % N_BASE
    body = (
        f"package giant.lib.mid;\n\n"
        f"import giant.lib.base.B{a};\n"
        f"import giant.lib.base.B{b};\n\n"
        f"public class M{i} {{\n"
        f"    public static int value(int x) {{\n"
        f"        return B{a}.value(x) + B{b}.value(x + 1);\n"
        f"    }}\n"
    )
    if i == 0:
        body += (
            '    public static final String GREET = B0.GREET;\n'
        )
    body += "}\n"
    return body


def app_java(i: int) -> str:
    a, b = (5 * i) % N_MID, (5 * i + 77) % N_MID
    body = (
        f"package giant.lib.app;\n\n"
        f"import giant.lib.mid.M{a};\n"
        f"import giant.lib.mid.M{b};\n\n"
        f"public class A{i} {{\n"
        f"    public static int value(int x) {{\n"
        f"        return M{a}.value(x) + M{b}.value(x + 2);\n"
        f"    }}\n"
    )
    if i == 0:
        body += (
            '    public static final String GREET = M0.GREET;\n'
        )
    body += "}\n"
    return body


def java_main() -> str:
    checksum = " + ".join(f"A{i}.value({i})" for i in range(N_APP))
    imports = ["import giant.lib.app.A0;"]
    for i in range(1, N_APP):
        imports.append(f"import giant.lib.app.A{i};")
    return (
        "\n".join(imports)
        + "\n\npublic class Main {\n"
        + "    public static void main(String[] args) {\n"
        + f"        int checksum = {checksum};\n"
        + "        if (checksum == -1) System.out.print(\"\");\n"
        + "        System.out.println(A0.GREET);\n"
        + "    }\n"
        + "}\n"
    )


def base_js(i: int) -> str:
    body = (
        f"export function b{i}(x) {{\n"
        f"    let acc = x + {i};\n"
        f"    acc = (acc * 31 + {i * 7}) % 1000033;\n"
        f"    acc = (acc * 131 + x * 17 + {i}) % 1000199;\n"
        f"    return acc;\n"
        f"}}\n"
    )
    if i == 0:
        body += 'export const greeting = "Hello, world!";\n'
    return body


def mid_js(i: int) -> str:
    a, b = (3 * i) % N_BASE, (3 * i + 40) % N_BASE
    out = (
        f'import {{ b{a} }} from "../base/b{a}.mjs";\n'
        f'import {{ b{b} }} from "../base/b{b}.mjs";\n'
        f"export function m{i}(x) {{\n"
        f"    return b{a}(x) + b{b}(x + 1);\n"
        f"}}\n"
    )
    if i == 0:
        out += 'export { greeting } from "../base/b0.mjs";\n'
    return out


def app_js(i: int) -> str:
    a, b = (5 * i) % N_MID, (5 * i + 77) % N_MID
    out = (
        f'import {{ m{a} }} from "../mid/m{a}.mjs";\n'
        f'import {{ m{b} }} from "../mid/m{b}.mjs";\n'
        f"export function a{i}(x) {{\n"
        f"    return m{a}(x) + m{b}(x + 2);\n"
        f"}}\n"
    )
    if i == 0:
        out += 'export { greeting } from "../mid/m0.mjs";\n'
    return out


def js_entry() -> str:
    imports = ['import { greeting, a0 } from "./lib/app/a0.mjs";']
    for i in range(1, N_APP):
        imports.append(f'import {{ a{i} }} from "./lib/app/a{i}.mjs";')
    checksum = " + ".join(f"a{i}({i})" for i in range(N_APP))
    return (
        "\n".join(imports)
        + "\n\nconst checksum = "
        + checksum
        + ";\nconsole.log(greeting);\n"
        + "void checksum;\n"
    )


def base_py(i: int) -> str:
    body = (
        f"def b{i}(x):\n"
        f"    acc = x + {i}\n"
        f"    acc = (acc * 31 + {i * 7}) % 1000033\n"
        f"    acc = (acc * 131 + x * 17 + {i}) % 1000199\n"
        f"    return acc\n"
    )
    if i == 0:
        body += 'greeting = "Hello, world!"\n'
    return body


def mid_py(i: int) -> str:
    a, b = (3 * i) % N_BASE, (3 * i + 40) % N_BASE
    out = (
        f"from lib.base.b{a} import b{a}\n"
        f"from lib.base.b{b} import b{b}\n"
        f"def m{i}(x):\n"
        f"    return b{a}(x) + b{b}(x + 1)\n"
    )
    if i == 0:
        out += "from lib.base.b0 import greeting\n"
    return out


def app_py(i: int) -> str:
    a, b = (5 * i) % N_MID, (5 * i + 77) % N_MID
    out = (
        f"from lib.mid.m{a} import m{a}\n"
        f"from lib.mid.m{b} import m{b}\n"
        f"def a{i}(x):\n"
        f"    return m{a}(x) + m{b}(x + 2)\n"
    )
    if i == 0:
        out += "from lib.mid.m0 import greeting\n"
    return out


def py_entry() -> str:
    lines = ["from lib.app.a0 import greeting, a0"]
    for i in range(1, N_APP):
        lines.append(f"from lib.app.a{i} import a{i}")
    checksum = " + ".join(f"a{i}({i})" for i in range(N_APP))
    return (
        "\n".join(lines)
        + "\n\nchecksum = "
        + checksum
        + "\nprint(greeting)\n"
        + "_ = checksum\n"
    )


def write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def main() -> None:
    for i in range(N_BASE):
        write(HERE / "cp3" / "src" / "lib" / "base" / f"b{i}.cp3", base_cp3(i))
        write(HERE / "js" / "src" / "lib" / "base" / f"b{i}.mjs", base_js(i))
        write(HERE / "python" / "src" / "lib" / "base" / f"b{i}.py", base_py(i))
        write(HERE / "cpp" / "src" / "lib" / "base" / f"b{i}.hpp", base_cpp(i))
        write(HERE / "rust" / "src" / "base" / f"b{i}.rs", base_rust(i))
        write(HERE / "go" / "src" / "lib" / "base" / f"b{i}.go", base_go(i))
        write(HERE / "java" / "src" / "lib" / "base" / f"B{i}.java", base_java(i))
    for i in range(N_MID):
        write(HERE / "cp3" / "src" / "lib" / "mid" / f"m{i}.cp3", mid_cp3(i))
        write(HERE / "js" / "src" / "lib" / "mid" / f"m{i}.mjs", mid_js(i))
        write(HERE / "python" / "src" / "lib" / "mid" / f"m{i}.py", mid_py(i))
        write(HERE / "cpp" / "src" / "lib" / "mid" / f"m{i}.hpp", mid_cpp(i))
        write(HERE / "rust" / "src" / "mid" / f"m{i}.rs", mid_rust(i))
        write(HERE / "go" / "src" / "lib" / "mid" / f"m{i}.go", mid_go(i))
        write(HERE / "java" / "src" / "lib" / "mid" / f"M{i}.java", mid_java(i))
    for i in range(N_APP):
        write(HERE / "cp3" / "src" / "lib" / "app" / f"a{i}.cp3", app_cp3(i))
        write(HERE / "js" / "src" / "lib" / "app" / f"a{i}.mjs", app_js(i))
        write(HERE / "python" / "src" / "lib" / "app" / f"a{i}.py", app_py(i))
        write(HERE / "cpp" / "src" / "lib" / "app" / f"a{i}.hpp", app_cpp(i))
        write(HERE / "rust" / "src" / "app" / f"a{i}.rs", app_rust(i))
        write(HERE / "go" / "src" / "lib" / "app" / f"a{i}.go", app_go(i))
        write(HERE / "java" / "src" / "lib" / "app" / f"A{i}.java", app_java(i))
    write(HERE / "cp3" / "src" / "index.cp3", cp3_entry())
    write(HERE / "js" / "src" / "index.mjs", js_entry())
    write(HERE / "python" / "src" / "index.py", py_entry())
    write(HERE / "cpp" / "src" / "main.cpp", cpp_main())
    write(HERE / "rust" / "src" / "main.rs", rust_main())
    write(HERE / "go" / "src" / "main.go", go_main())
    write(HERE / "go" / "go.mod", "module giant\n\ngo 1.21\n")
    write(HERE / "java" / "src" / "Main.java", java_main())
    write(HERE / "python" / "src" / "lib" / "__init__.py", "")
    write(HERE / "python" / "src" / "lib" / "base" / "__init__.py", "")
    write(HERE / "python" / "src" / "lib" / "mid" / "__init__.py", "")
    write(HERE / "python" / "src" / "lib" / "app" / "__init__.py", "")
    write(HERE / "cp3" / "package.json",
          '{ "name": "bench-giant-repo-dev-reload",\n'
          '  "version": "0.1.0",\n'
          '  "description": "edit -> rebuild -> rerun on a 300-file module graph (giant-repo-dev-reload)",\n'
          '  "type": "application",\n'
          '  "main": "src/index.cp3" }\n')
    print(f"giant-repo-dev-reload fixture written: {N_BASE + N_MID + N_APP + 7} files x 7 languages")


if __name__ == "__main__":
    main()
