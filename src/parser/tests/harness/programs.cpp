// =============================================================================
// src/parser/tests/programs.cpp - built-in representative programs.
//
// The driver runs these when invoked with no file argument. Each program is
// stored as an array of LINES (rather than one big raw string literal) so the
// layout linter masks the text as ordinary string contents and does not
// mis-track braces that appear inside the snippet.
// =============================================================================
#include "programs.h"

#include <cstddef>

namespace cp3test {
namespace {

const char* const kModuleItems[] = {
    "import console from \"std/console\";",
    "import { readFile, writeFile as wf } from \"std/fs\";",
    "import * as math from \"std/math\";",
    "import \"std/side\";",
    "import native \"lib\" as nativeLib;",
    "import \"cpp/iostream\" as io;",
    "",
    "export const PI: number = 3.14;",
    "export function add(a: number, b: number): number { return a + b; }",
    "export default 42;",
    "export { PI as p, add };",
    "export * from \"std/other\";",
};

const char* const kDeclarations[] = {
    "let x = 1, y: string = \"s\";",
    "const { a, b: c, ...rest } = obj;",
    "var [p, , q = 5, ...tail] = arr;",
    "",
    "interface Shape<T extends Base = Box> extends Other {",
    "  area: number;",
    "  function grow(factor: number): Shape<T>;",
    "  readonly id?: string;",
    "}",
    "",
    "enum Color { Red, Green = 2, Blue }",
    "enum Status { Active = \"active\", Inactive = \"inactive\" }",
    "",
    "type Handler<E> = (event: E, index: number) => void;",
    "type Maybe = number | string | undefined;",
};

const char* const kClasses[] = {
    "abstract class Base<T> extends Parent implements IFoo, IBar {",
    "  public static readonly VERSION: number = 1;",
    "  weak parent: Parent;",
    "  private name?: string;",
    "  protected items: number[] = [];",
    "",
    "  constructor(x: number, ref y: string) {",
    "    this.name = y;",
    "  }",
    "",
    "  abstract function area(): number;",
    "",
    "  function get_name(): string { return this.name; }",
    "",
    "  get size(): number { return this.items.length; }",
    "  set size(v: number) { this.items.push(v); }",
    "",
    "  async function load(): Promise<void> { await fetch(); }",
    "}",
};

const char* const kStatements[] = {
    "function controlFlow(n: number): number {",
    "  let sum = 0;",
    "  for (let i = 0; i < n; i++) {",
    "    if (i % 2 === 0) continue;",
    "    sum += i;",
    "  }",
    "  for (const item of items) { console.log(item); }",
    "  for (const key in obj) { }",
    "  while (sum > 0) { sum--; }",
    "  do { sum++; } while (sum < 3);",
    "  switch (n) {",
    "    case 1: sum = 1; break;",
    "    case 2:",
    "    case 3: sum = 2; break;",
    "    default: sum = -1;",
    "  }",
    "  outer: for (;;) { break outer; }",
    "  try { risky(); } catch (e) { handle(e); } finally { cleanup(); }",
    "  throw new Error(\"boom\");",
    "  return sum;",
    "}",
};

const char* const kwebserverions[] = {
    "let a = 1 + 2 * 3 ** 2 - 4 / 5 % 6;",
    "let b = x << 2 | y & 3 ^ ~z;",
    "let c = p && q || r ?? s;",
    "let d = o?.field?.[index]?.(arg);",
    "let e = cond ? whenTrue : whenFalse;",
    "let f = a === b !== c == d;",
    "let g = !flag ? -x : +y;",
    "let h = obj.method<number>(1, ...args);",
    "let i = new Box<number>(1, 2);",
    "let j = 0..numbers.length;",
    "let k = { name, \"quoted\": 1, [computed]: 2, ...spread, method: () => 1 };",
    "let l = (a, b) => a + b;",
    "let m = async (x: number): Promise<number> => { return x; };",
    "let n = `tpl ${a + b} mid ${c} end`;",
    "let o = { weak: target };",
    "let p2 = [1, , 3, ...rest];",
    "let q2 = /ab+c/gi;",
    "a >>>= 2;",
    "b >>> c;",
    "await doThing();",
    "parallel.for(0..n, (i) => worker(data[i]));",
};

const char* const kTypes[] = {
    "type T1 = number;",
    "type T2 = string[];",
    "type T3 = [string, number, boolean];",
    "type T4 = { a: number, function b(x: string): void };",
    "type T5 = () => void;",
    "type T6 = (a: number, b?: string) => Promise<void>;",
    "type T7 = r_'u32' | c_'long long' | ts_'number';",
    "type T8 = \"literal\" | 42 | true;",
    "type T9 = (A | B)[];",
    "type T10 = Map<string, Array<Box<number>>>;",
    "type Nested = Box<Box<Array<number>>>;",
};

const char* const kErrors[] = {
    "let x = ;",
    "let y = 1",
};

const char* const kEdge[] = {
    "new Foo;",
    "new ns.Box<number>(1);",
    "const re2 = /[/]/g;",
    "x ?\?= 1;",
    "y &&= 2;",
    "z ||= 3;",
    "async function main(): Promise<void> { for await (const c of stream) { await c; } }",
    "class C { function [Symbol.iterator](): Iterator { } static function make(): C "
    "{ return new C(); } }",
    "interface I { log(x: string): void; readonly id: number; }",
    "let tpl = `a${ `inner ${x}` }b`;",
    "let del = delete obj.prop;",
    "let tp = typeof value;",
};

struct CaseData {
    const char* name;
    const char* const* lines;
    std::size_t count;
};

const CaseData kCases[] = {
    {"module-items", kModuleItems, 12},
    {"declarations", kDeclarations, 15},
    {"classes", kClasses, 19},
    {"statements", kStatements, 21},
    {"webserverions", kwebserverions, 21},
    {"types", kTypes, 11},
    {"errors", kErrors, 2},
    {"edge", kEdge, 12},
};

}  // namespace

void builtinCases(std::vector<Case>& out) {
    for (const CaseData& c : kCases) {
        std::string src;
        for (std::size_t i = 0; i < c.count; ++i) {
            src += c.lines[i];
            src += '\n';
        }
        out.emplace_back(c.name, std::move(src));
    }
}

}  // namespace cp3test
