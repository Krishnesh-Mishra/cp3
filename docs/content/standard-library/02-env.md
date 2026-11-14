---
title: Environment variables
chapter: Chapter 3 · Standard library
---
# Environment variables

`std/env` exports the `env` namespace as a named import:

```ts
import { env } from "std/env";
```

Environment values arrive from the operating system as strings. `env.get<T>` can convert a value to the requested type, but it is not a schema system: validate application-specific ranges and formats yourself.

Configuration loading is best done at the program boundary, before modules consume settings. Read once, validate once, and pass a typed configuration object to the rest of the application. That keeps missing-variable behavior and conversion errors in one place.

## `env.get<T>(key, options?)`

Reads a variable and returns its value as `T`. With no explicit type argument and no default, the result is a string. When a `default` value is provided, its type is inferred if `T` was omitted.

```ts
const host = env.get<string>("APP_HOST");
const port = env.get<number>("APP_PORT", { default: 8080 });
const debug = env.get<boolean>("APP_DEBUG", { default: false });
```

If the variable is absent and no default exists, the call throws. Catch that exception when absence is a recoverable configuration choice. The options object currently documents `default`; do not assume Node's overloads or shell expansion rules.

For a configuration that should be optional, check first:

```ts
if (env.has("APP_REGION")) {
  const region = env.get<string>("APP_REGION");
  console.log(region);
}
```

Conversion failures depend on the target type and runtime conversion rules. Prefer to parse untrusted configuration deliberately when malformed values require a custom error message.

## `env.has(key)`

Returns whether the named variable is set in the current process environment:

```ts
if (!env.has("DATABASE_URL")) {
  throw new Error("DATABASE_URL must be configured");
}
```

Presence is governed by the runtime's environment map, which is seeded from the operating-system environment on first use. An OS variable that is unset — or present but empty (`KEY=`) — is treated as **absent**, so `env.has` reports `false` for it. A value written through `env.load` (even an empty one) **is** tracked by the map, so `env.has` reports `true` for it. The same rule applies to `process.env.set` writes.

```ts
// OS: APP_HOST=   (empty)
env.has("APP_HOST");   // false
env.load(".env");      // .env contains APP_HOST= (empty)
env.has("APP_HOST");   // true
```

This distinction matters when an empty value intentionally disables a setting: an empty OS value already looks absent to `env.has`, and a non-empty value is required to pass a presence check. To require a non-empty value, check content as well — `has` is a presence check against the map, not full validation of the value.

## `env.load(path)`

Reads a `.env`-style file and loads its entries into `process.env`. It throws if the file cannot be opened. Call it before reading values that should come from that file:

```ts
env.load(".env");
const endpoint = env.get<string>("SERVICE_URL");
```

The path is interpreted relative to the process working directory unless it is absolute. Keep secrets out of source control; this API does not encrypt the file or redact values from later program output.

Loading a file populates the current process environment; it does not modify the parent shell or write values back to the file. Avoid logging a loaded environment wholesale because it may contain credentials.

## A small configuration boundary

```ts
interface AppConfig {
  host: string;
  port: number;
  verbose: boolean;
}

function loadConfig(): AppConfig {
  return {
    host: env.get<string>("APP_HOST", { default: "127.0.0.1" }),
    port: env.get<number>("APP_PORT", { default: 8080 }),
    verbose: env.get<boolean>("APP_VERBOSE", { default: false }),
  };
}
```

The example demonstrates where defaults belong; it does not make every string a valid value for every type. If conversion fails or the port is outside the application's accepted range, report that at this boundary before starting the service.

## Relationship to `process.env`

`std/process` exposes an environment map on the process namespace. `std/env` is the helper API for presence checks, typed reads, defaults, and loading a file. Their lookup surfaces are related, but they are not the same type or the same import.

## API summary

| Call | Result | Missing-value behavior |
| --- | --- | --- |
| `env.get<T>(key)` | `T` (string when omitted) | Throws if missing |
| `env.get<T>(key, { default })` | `T` | Returns default if missing |
| `env.has(key)` | `boolean` | Returns false when the key is missing or the OS value is empty; true for values written via `env.load`/`process.env.set` (even empty) |
| `env.load(path)` | `void` | Throws if file cannot be read |

The compiler's supported members are exactly `get`, `has`, and `load`; unknown `env` members are diagnosed rather than treated as arbitrary properties.
