---
title: Environment
---
# Environment

`std/env` reads the process environment and loads `.env` files. Import the `env` namespace as a named import:

```ts
import { env } from "std/env";
```

Environment values arrive as strings. `env.get<T>` converts on read but is not a schema system — validate application ranges and formats yourself. The compiler's supported members are exactly `get`, `has` and `load`; any other `env` member is a diagnostic, not a property lookup.

## Members

| Member | Signature | Notes |
| --- | --- | --- |
| `env.get<T>` | `get<T>(key: string): T` | Reads a variable as `T`; string when `T` is omitted |
| `env.get<T>` | `get<T>(key: string, options: { default: T }): T` | Returns `options.default` when missing; `T` is inferred from the default when omitted |
| `env.has` | `has(key: string): boolean` | Presence check against the runtime environment map |
| `env.load` | `load(path: string): void` | Reads a `.env`-style file into `process.env`; throws if the file cannot be opened; path relative to the working directory unless absolute |

## Missing-value semantics

| Call | Missing-value behavior |
| --- | --- |
| `env.get<T>(key)` | Throws when the variable is absent and no default is provided |
| `env.get<T>(key, { default })` | Returns the default |
| `env.has(key)` | `false` when the key is missing, or the OS value is present but empty (`KEY=`); `true` for values written via `env.load` / `process.env.set`, even when empty |
| `env.load(path)` | Throws if the file cannot be read |

Absence-as-empty is governed by the seeding rule: an empty OS variable looks absent to `has`, while any value written through `load` (even empty) is tracked by the map. To require a non-empty value, check content as well. `env.load` populates the current process only; it does not touch the parent shell or the file. `std/env` is not the `process.env` map — see [Process](./process.md) for the map itself, and note their lookup surfaces are related but not the same type or import.

Behavior details: [guide](../../standard-library/02-env.md)
