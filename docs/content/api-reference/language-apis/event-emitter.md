---
title: EventEmitter
---
# EventEmitter

`EventEmitter` is a language builtin — no import. `new EventEmitter()` is the untyped bus; `new EventEmitter<Events>()` is the typed bus where an interface `Events` map defines the legal event names and payloads. See the [global builtins guide](../../language/99-global-builtins.md) for the full typed-map rules.

## Untyped bus

| Member | Signature | Notes |
| --- | --- | --- |
| `on` | `on(event: string, listener): undefined` | Registers `listener`; fires on every matching `emit`. |
| `once` | `once(event: string, listener): undefined` | Fires at most once, then removed. |
| `off` | `off(event: string, listener): undefined` | Removes the listener that is the same box registered for the event. |
| `emit` | `emit(event: string, ...args): undefined` | Calls every listener for `event`, in order. |
| `onAny` | `onAny(listener: (name: string, ...args) => undefined): undefined` | Wildcard channel; only legal on a map with an index signature. |
| `offAny` | `offAny(listener): undefined` | Removes a wildcard listener. |
| `isValidEventName` | `isValidEventName(name: string): boolean` | True when `name` is declared (any name, with an index signature); narrows `name` in the guarded branch. |

Payloads are not checked on the untyped bus — annotate the listener parameters where you need them.

An `emit` carries up to 8 values. A listener takes the values it declares: a nullary listener takes none; a listener declaring more parameters than were emitted sees `undefined` for the missing ones (JS parity). Listener identity is by box — two inline arrows are two different listeners, so `off("go", () => { ... })` never removes an earlier `on("go", () => { ... })`. Keep a named reference.

```ts
const bus = new EventEmitter();
bus.on("go", (a: string, b: number) => {
  console.log(`${a} ${b}`);
});
bus.emit("go", "x", 1);
```

## Typed bus: `EventEmitter<Events>`

With an interface `Events` map, `on`/`once`/`off`/`emit` are checked against the map at compile time:

```ts
interface Events {
  hello: (m: Msg) => undefined;
  logout: () => undefined;
}

const bus = new EventEmitter<Events>();
bus.on("hello", (m) => console.log(m.text)); // m is Msg
bus.emit("hello", { id: 1, text: "hi" });
bus.on("the-event", (ev) => console.log(ev)); // rejected: CP3305
bus.emit("hello", "wrong payload");           // rejected: CP3401
```

Rules:

- The event name must name a member of the map — a string literal, a literal-typed variable, or an all-literal union — or the map must declare an index signature (`[name: string]: (m: Msg) => undefined`), which accepts any dynamic name.
- Listener parameters are contextually typed from the map; annotations must agree with the map (mutual assignability), and a handler may not declare more parameters than the map provides.
- Unknown event names are rejected with `CP3305`; a wrong payload count with `CP3403`; a payload or listener shape that does not fit the map's listener signature with `CP3401`.
- A dynamic name on a member-only map is rejected at the `emit` site with `CP3401` ("the Events map has concrete event names"). Guard with `if (bus.isValidEventName(name))` — the guarded branch narrows `name` — or wrap the emit in `try`/`catch`.
- `onAny`/`offAny` are legal only with an index signature; on a member-only map they are rejected (all events are named, so a wildcard can never fire).
- A fresh literal payload to `bus.emit` is checked against the map's parameter types, field by field.
- `bus.isValidEventName(name)` returns `boolean` and narrows `name` in the guarded branch.

## Diagnostics

| Code | Trigger |
| --- | --- |
| `CP3305` | Unknown event name / unknown member on a closed surface. |
| `CP3401` | Payload or listener shape does not fit the map's listener signature (also: a dynamic name where the map has only concrete events). |
| `CP3403` | Payload count mismatch against the map's listener signature. |

## Related pages

- [Global builtins guide](../../language/99-global-builtins.md) — the checked surface and typed-map rules
- [Errors](../../language/06-modules-and-errors/02-errors.md)
