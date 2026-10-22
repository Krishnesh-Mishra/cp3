# 24. Doc comments

A **doc comment** documents a declaration. The editor reads it and shows it
where the declaration is used: on hover, in the completion popup and in the
signature help.

Doc comments are JSDoc-style, exactly like TypeScript's, so a `.cp3` file
reads the same way a `.ts` file does.

## The two spellings

```cp3
/**
 * Adds two numbers.
 * @param a The first number.
 * @param b The second number.
 * @returns The sum of a and b.
 */
function add(a: number, b: number): number {
  return a + b;
}

/// Greets the world.
function greet(): string {
  return "hello";
}
```

- **`/** ... */`** - the block form. The comment documents the declaration
  that follows it.
- **`/// ...`** - the line form. Consecutive `///` lines form one comment and
  document the declaration that follows them.

A doc comment documents the **declaration that follows it**: a function, a
class, a method, a class property or a variable declaration.

```cp3
/// The greeter used by the CLI.
class Greeter {
  /// Greets the given name.
  function speak(name: string): string {
    return "hi " + name;
  }
}

/// How many times the CLI said hello.
let greets = 0;
```

## The tags

| Tag | Meaning |
| --- | --- |
| *(free text)* | The description of the declaration. |
| `@param <name> <text>` | What one parameter is for. |
| `@returns` / `@return <text>` | What the declaration returns. |

A tag's text continues on the following lines until the next tag. Any other
`@tag` is kept as plain text - an unknown tag is never an error.

```cp3
/**
 * Reads a file and parses it.
 *
 * @param path The file to read.
 * @param encoding The text encoding; "utf8" by default.
 * @returns The parsed value.
 * @throws When the file cannot be read.
 */
function readJsonFile(path: string, encoding?: string): unknown {
  // ...
}
```

## What a doc comment is NOT

- It is **documentation, not behaviour**. A program with doc comments
  compiles to exactly the same output as the same program without them.
- It never produces a diagnostic. A doc comment above something the compiler
  does not know is simply dropped - there is no "unused doc comment" error.
- It does not change the type of anything. The description is text for the
  reader; the compiler never checks it.

## Where the documentation shows up

The editor (VS Code, through the language server built into `cp3c`) shows the
documentation of a symbol:

- **hover** over a name shows the description, the `@param` list and the
  `@returns` text;
- the **completion popup** shows the description of every candidate;
- the **signature help** shows the description of the callee and what each
  parameter is for;
- the **standard library** members (`console.log`, `Math.abs`, `path.join`,
  ...) carry their own documentation, written from
  [the standard library reference](../standard-library/04-imported-std-modules.md),
  so hovering a std member explains what it does even when the program has no
  doc comment of its own.
