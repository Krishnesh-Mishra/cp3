---
title: Paths
---
# Paths

`std/path` provides platform-aware path operations through its default namespace:

```ts
import path from "std/path";

const config = path.join("config", "local.json");
```

Use these functions to manipulate path syntax. They do not check whether a path exists, whether the current user can access it, or whether a symbolic link points outside a directory.

## Platform separator: `path.sep`

The `sep` property is the path separator for the current platform. Prefer `join` when constructing a path so application code does not need to branch on the platform.

## Constructing paths

### `path.join(...parts)`

Joins path parts using the platform separator:

```ts
const output = path.join("build", "generated", "index.cpp");
```

### `path.resolve(...parts)`

Resolves path parts into an absolute path and normalizes `.` and `..` segments. Resolution uses the process working directory when the parts do not begin with an absolute path.

```ts
const absolute = path.resolve("build", "generated");
```

### `path.normalize(p)`

Normalizes path syntax and resolves `.` and `..` segments without promising that the path exists or that two different spellings identify the same filesystem object.

## Inspecting paths

| Member | Result | Meaning |
| --- | --- | --- |
| `basename(p, ext?)` | `string` | Final path component, optionally with a matching extension removed |
| `dirname(p)` | `string` | Directory component |
| `extname(p)` | `string` | Extension including the leading dot, or empty string |
| `isAbsolute(p)` | `boolean` | Whether the path is absolute for this platform |

```ts
path.basename("src/main.cp3"); // "main.cp3"
path.extname("src/main.cp3"); // ".cp3"
path.dirname("src/main.cp3"); // "src"
```

`basename` can be given an extension to remove from the result:

```ts
path.basename("archive.tar", ".tar"); // "archive"
```

## Security note

Normalization is lexical. It does not resolve symbolic links and is not a substitute for validating a path against an allowed directory after filesystem resolution. Do not use string-prefix checks alone to authorize access to sensitive files.

## Supported surface

The closed namespace contains `sep`, `join`, `resolve`, `extname`, `dirname`, `normalize`, `basename`, and `isAbsolute`. A misspelled or unsupported member is reported by the compiler.

## Related pages

- [File system](./04-fs.md) — operations that take the paths built here
- [URL and query strings](./19-url.md) — percent-encoding and URL handling, a different surface
