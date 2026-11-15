---
title: Path
---
# Path

`std/path` provides platform-aware path operations through its default namespace:

```ts
import path from "std/path";
```

These functions manipulate path syntax only — they do not check existence, access, or symlinks. The namespace is closed; an unsupported or misspelled member is a compile-time diagnostic.

## Members

| Member | Signature | Notes |
| --- | --- | --- |
| `sep` | `sep: string` | Path separator for the current platform |
| `join` | `join(...parts: string[]): string` | Joins parts with the platform separator |
| `resolve` | `resolve(...parts: string[]): string` | Absolute path; resolves `.`/`..`; uses the working directory when no part begins with an absolute path |
| `normalize` | `normalize(p: string): string` | Removes redundant separators and resolves `.`/`..` segments lexically; no existence guarantee |
| `basename` | `basename(p: string, ext?: string): string` | Final path component; with `ext`, a matching extension is removed |
| `dirname` | `dirname(p: string): string` | Directory component |
| `extname` | `extname(p: string): string` | Extension including the leading dot; `""` when none |
| `isAbsolute` | `isAbsolute(p: string): boolean` | Whether `p` is absolute for this platform |

There is no `parse`, `format`, or `delimiter` in this module. Normalization is lexical and is not a substitute for validating a path after filesystem resolution.

Behavior details: [guide](../../standard-library/05-path.md)
