---
title: Operating system
---
# Operating system

`std/os` reports information about the runtime host. Import the default namespace:

```ts
import os from "std/os";
```

Values describe the machine that runs the executable, not the one that compiled it. A `typescript` project keeps `number`; a `rust`/`cpp` project uses exact widths (`cpus()` is `usize`, memory readings are `u64`).

## Members

| Member | Signature | Notes |
| --- | --- | --- |
| `platform` | `platform(): string` | One of `"windows"`, `"macos"`, `"linux"` |
| `hostname` | `hostname(): string` | Machine host name; diagnostics, not an authenticated identity |
| `homedir` | `homedir(): string` | Current user's home-directory path |
| `tmpdir` | `tmpdir(): string` | System temporary-directory path |
| `cpus` | `cpus(): number` | Logical CPU count, not a scheduling guarantee |
| `totalMemory` | `totalMemory(): number` | Total physical system memory in bytes |
| `freeMemory` | `freeMemory(): number` | Snapshot of free system memory in bytes |
| `totalmem` | `totalmem(): number` | Node alias of `totalMemory()` |
| `freemem` | `freemem(): number` | Node alias of `freeMemory()` |
| `loadavg` | `loadavg(): [number, number, number]` | 1/5/15-minute load averages; platforms without load averages report zeros |
| `networkInterfaces` | `networkInterfaces(): Array<[string, string, string, boolean]>` | One `[name, address, family, internal]` tuple per interface; `family` is `"IPv4"` or `"IPv6"`; `internal` marks loopback-only interfaces |

These are the supported members — there is no `type`, `release`, `arch`, `userInfo`, or `EOL` in `std/os` (`arch` lives on [Process](./process.md)). Memory readings are snapshots and may change immediately; not a reservation mechanism. Compose paths from `homedir()`/`tmpdir()` with `std/path`; the paths are not open handles or permission grants, and a temporary path may collide — create unique names.

Behavior details: [guide](../../standard-library/06-os.md)
