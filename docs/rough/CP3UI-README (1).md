# C+++ UI (`cp3ui`) - React-like Native UI Framework for C+++

> Companion spec to the core language (`README.md`), tooling (`CLI-README.md`), and stdlib (`STDLIB-README.md`). Defines `cp3ui`: a React/React-Native-style declarative UI framework for cp3, rendering through Slint's GPU pipeline. It has no language syntax or CLI mechanics of its own beyond what's specified here - for the compiler, memory model, or `cp3` command surface, see the other three documents.

## Status

**Post-beta-one.** The core language, CLI, and stdlib ship first. `cp3ui` is the first major third-party `.cp3pak` built on top of them - not part of the compiler or the stdlib, and it requires nothing from either beyond what's already locked in `README.md`/`CLI-README.md`. This document is locked for implementation once beta one ships: build against it directly, and stop to ask rather than silently assuming anything not decided here.

---

## 1. What `cp3ui` is

`cp3ui` is a declarative, component-based UI framework for cp3. You write `.cpx` files (JSX-like syntax), a build-time transform converts them to ordinary cp3 function calls, a pure-cp3 reconciler diffs the virtual tree, and a thin hand-written C++ bridge drives Slint on a dedicated UI thread.

It is **not** part of the compiler or the stdlib. It ships as an ordinary third-party `.cp3pak`, installed via `cp3 install cp3ui` like any other package (`CLI-README.md` §4).

The positioning, stated as a table because it's the entire reason this exists:

| Framework | DX | Idle RAM | Native/GPU render | GC pauses |
|---|---|---|---|---|
| Electron | React/web | 150-400 MB | web page in a frame | V8 GC |
| Tauri | React/web | 30-60 MB | web page in a frame | webview JS GC |
| Flutter | Dart | 60-120 MB | custom canvas | Dart GC |
| SwiftUI | Swift | 10-20 MB | native | none (ARC) |
| **cp3ui** | **React/JSX** | **10-20 MB** | **GPU (Skia via Slint)** | **none (ARC + RAII)** |

The pitch: React developer experience, Swift-class memory footprint, zero GC pauses, one codebase for Windows/macOS/Linux.

### Why not the alternatives - logged so they aren't re-derived from scratch later

This design went through two earlier, explicitly rejected shapes before landing here. Both are dead ends, not options to revisit:

- **A Qt-backed `cp3ui`** (hand-rolled `QWidget` bridge) - rejected. Qt's MOC preprocessor produces metadata `libclang`-based bindgen can never see (`CLI-README.md`'s post-beta-one roadmap has the full mechanical reason), Qt's own event loop conflicts with cp3's libuv one, and a Qt baseline alone runs 25-45 MB before a single widget is drawn - directly against `cp3ui`'s lean positioning above. **The project still has a separate, real plan for a hand-authored Qt bridge** (`CLI-README.md`'s post-beta-one roadmap) for people who want actual Qt - that effort is unrelated to `cp3ui` and doesn't feed into it.
- **A webview-backed `cp3ui`** (HTML/CSS rendered inside an OS webview, Tauri-style) - rejected, explicitly, by the project owner. Not a backend under consideration anywhere in this document. (`std/webview` itself stays dropped from the stdlib too, per `STDLIB-README.md` - this is a separate, independent rejection of the same underlying idea for a different reason.)
- **Raw Skia, hand-rolled** - considered and passed over. This would mean hand-building layout, text shaping, and widget hit-testing that Slint already provides - years of extra engineering for the same RAM footprint Slint already hits.

## 2. Architecture - why Slint

```
┌──────────────────────────────────────────────────────────┐
│  Developer code (.cpx)                                    │
│  <Box style={{ height: isActive ? 100 : 200 }}>...</Box>  │
├──────────────────────────────────────────────────────────┤
│  transform-cpx (build-time, ships inside cp3ui's .cp3pak) │
│  JSX → pure cp3 function calls + source map                │
├──────────────────────────────────────────────────────────┤
│  cp3 compiler (UNCHANGED - it never sees JSX)              │
│  .cp3 → C++20 → Clang/GCC/MSVC                             │
├──────────────────────────────────────────────────────────┤
│  cp3ui runtime (pure cp3)                                  │
│  Reconciler · Hooks · StyleSheet · Synthetic events        │
│  runs on the cp3/libuv thread                               │
├──────────────────────────────────────────────────────────┤
│  cp3ui bridge (hand-written C++, statically linked)        │
│  Two Boost.Lockfree queues (already bundled by cp3)         │
├──────────────────────────────────────────────────────────┤
│  Slint UI thread                                            │
│  Reactive properties · Native layouts · GPU rendering       │
├──────────────────────────────────────────────────────────┤
│  GPU (Skia renderer: Metal / Vulkan / OpenGL / D3D)         │
└──────────────────────────────────────────────────────────┘
```

Why Slint specifically, decided and locked:

- **No GC.** Slint is Rust-core with RAII C++ bindings - the same philosophy as cp3's own ARC + RAII model (`README.md` §10). No runtime pauses on the render thread, ever.
- **Reactive property bindings.** State-driven styling (`background: isActive ? A : B`) maps directly to Slint's native bindings - no CSS parser, no JIT string scanning of any kind.
- **Native animation primitives.** `animate property { duration; easing; }` runs GPU interpolation independent of cp3 logic - see §8.
- **Embedded-grade footprint.** Slint runs on microcontrollers; a desktop app using Slint's Skia renderer lands in the 10-20 MB idle-RAM range.
- **A real C++ API.** cp3 already transpiles to C++20 (`CLI-README.md` §1) - linking Slint's C++ API is an ordinary C++ integration, no FFI gymnastics beyond what `import native` already handles.

## 3. Compilation pipeline - zero compiler changes

`cp3ui` uses only mechanisms the cp3 CLI already has (or, for `cp3 exec`/`"bin"`, mechanisms added to `CLI-README.md` specifically because `cp3ui` needed them - see §11). No new compiler feature, no new `package.json` field beyond what `CLI-README.md` already documents.

```
src/App.cpx                          ← developer writes JSX-like code
src/main.cp3                         ← ordinary cp3
        ↓
package.json "scripts": { "prebuild": "cp3 exec cp3ui transform" }
        ↓
cp3 exec resolves cp3ui's "bin" entry (CLI-README.md §5.1/§1),
builds + caches it, and runs it as transform-cpx
        ↓
transform-cpx (bundled inside cp3ui's .cp3pak)
   • parses .cpx JSX
   • emits pure cp3 function calls
   • emits a source map (.cpx ↔ generated .cp3)
   • output lands in .cp3/build/cpx-output/
        ↓
cp3 build                            ← existing CLI, untouched
   parse → type-check → C++20 → toolchain → single native executable
   (error locations mapped back to the original .cpx line via the source map,
    same source-mapping machinery CLI-README.md §2 already requires)
```

What the transform does, concretely:

```jsx
// src/App.cpx (what the developer writes)
<Box style={{ height: isActive ? 100 : 200 }}>
  <Text>Hello {name}</Text>
  <Button onClick={() => setCount(count + 1)}>Click</Button>
</Box>
```
```cp3
// generated pure cp3 (what the compiler sees)
Box({
  style: { height: isActive ? 100 : 200 },
  children: [
    Text({ children: ["Hello ", name] }),
    Button({
      onClick: () => setCount(count + 1),
      children: ["Click"]
    })
  ]
})
```

JSX expression rules (matching real JSX, deliberately):

- Anything valid as a cp3 expression is allowed inside `{}` - literals, variables, arithmetic, ternaries, function calls, arrow functions (including multi-statement bodies `() => { ... }`), template literals, nested JSX.
- Statements (`let`, `if`, `for`, `return`) are **not** expressions - same rule as real JSX. Wrap in an arrow function if needed.
- Component names start uppercase (`Box`, `Text`); lowercase names are reserved for future native-element passthrough.

## 4. Threading model - two threads, two lock-free queues

```
Thread 1 - cp3 / libuv (the LOGIC thread)
  • All user code: functions, async/await, fetch, file I/O, workers
  • Hooks state (useState/useEffect live here)
  • The reconciler: diffs virtual trees, emits commands
  • Heavy computation here NEVER blocks the UI

        ⇅  Boost.Lockfree queues (cp3 already bundles this - CLI-README.md §3)
        ⇅  queue A: commands (cp3 → Slint)  CREATE/UPDATE/DELETE/REORDER
        ⇅  queue B: events   (Slint → cp3)  onClick/onChange/onKeyDown/...

Thread 2 - Slint (the UI thread)
  • Runs Slint's own event loop (its own thread - NOT libuv's)
  • Owns every Slint component instance
  • Drains queue A, applies mutations inside slint::invoke_from_event_loop
  • Handles OS input events, posts them to queue B
  • Drives the GPU render loop (Skia: Metal/Vulkan/OpenGL/D3D)
```

**The iron rule, stated once and never violated: Slint component handles are not thread-safe.** The cp3 thread never calls a Slint setter directly. Every mutation is a message; every message is applied on the Slint thread. Violating this is a crash or memory corruption, not a style issue - this is the single most important implementation rule in the whole package.

Shutdown ordering: cp3's event loop drains first, sends `SHUTDOWN` over queue A, the Slint thread exits its loop, then the process exits. No mid-frame teardown.

## 5. Frame pacing - the FPS policy

Locked decision:

- **Default: pace to display refresh.** `cp3ui` queries the monitor's refresh rate at window creation (60 / 120 / 144 / 240 Hz) and renders exactly at that rate. A 60Hz panel gets 60fps; a 240Hz panel gets 240fps. Automatic, per-monitor - this is standard VSync/pace-to-display behavior, the same thing every serious rendering engine does by default.
- **The render loop is intentionally trivial** - drain queue A, apply mutations, let Slint render. No heavy work ever happens on the UI thread. This is what makes high-refresh panels actually work: on a capable GPU (RTX 3050+, Apple M-series and comparable) the frame cost of a typical UI scene is microseconds, so 240fps is headroom, not effort.
- **Opt-in uncapped mode**, for benchmarking only:
  ```cp3
  cp3ui.setFramePolicy("uncapped")   // default is "vsync"
  ```
  Stated plainly so nobody over-promises: uncapped mode can push very high frame rates on strong GPUs on trivial scenes, but beyond the display's actual refresh rate it produces no visible frames and burns power/thermals for nothing - directly against `cp3ui`'s lean positioning (§1). Vsync-by-default is the product decision; uncapped exists for profiling and nothing else.

## 6. Component API

Components are ordinary cp3 functions returning virtual nodes. Availability is phased - see the roadmap (§12) - a component or hook listed here as **(Phase 2)** does not exist in the first shipped version.

### Core components

| Component | Purpose | Key props | Availability |
|---|---|---|---|
| `Window` | Root window (exactly one, top-level) | `title`, `width`, `height` | Phase 1 |
| `Box` | Container | `style`, `className`, `onClick`, `children` | Phase 1 |
| `Text` | Text label | `style`, `children` | Phase 1 |
| `Button` | Clickable button | `style`, `onClick`, `disabled` | Phase 1 |
| `Input` | Single-line text input | `value`, `placeholder`, `onChange`, `onFocus`, `onBlur` | Phase 1 |
| `TextArea` | Multi-line input | same as `Input` | Phase 2 |
| `Image` | Raster image | `src`, `style` | Phase 2 |
| `ScrollView` | Scrollable container | `style`, `children` | Phase 2 |
| `List` | Keyed, virtualizable list of items | `data`, `renderItem` | Phase 2 |
| `Checkbox` / `Radio` / `Slider` / `Select` | Form controls | `value`, `onChange` | Phase 2 |
| `Modal` | Overlay dialog | `isOpen`, `onClose` | Phase 2 |

### Events (all plain cp3 callbacks as props)

`onClick`, `onDoubleClick`, `onMouseEnter`, `onMouseLeave`, `onMouseMove`, `onMouseDown`, `onMouseUp`, `onChange`, `onInput`, `onFocus`, `onBlur`, `onKeyDown`, `onKeyUp`.

**Phase 1** ships `onClick`, `onChange`, `onFocus`, `onBlur` only - enough to make the core components in the table above actually interactive. **Phase 2** ships the rest of the set, plus synthetic bubbling.

**Event bubbling is synthetic, and that's real work, logged explicitly:** Slint's native events don't bubble. The reconciler implements a DOM-style propagation layer: a click on a child walks up the virtual tree firing each ancestor's handler, with `event.stopPropagation()` supported. This is roughly 500 lines of cp3, and it's specced here so it's budgeted rather than discovered mid-build. It ships in Phase 2 - Phase 1's four events are common enough, and simple enough, to ship as direct (non-bubbling) callbacks first.

### Hooks (pure cp3, React-shaped)

```cp3
let [count, setCount] = useState(0);                          // Phase 1
useEffect(() => { /* mount */ return () => { /* cleanup */ }; }, []);   // Phase 2
let expensive = useMemo(() => compute(value), [value]);        // Phase 2
let handler = useCallback(() => doThing(id), [id]);            // Phase 2
let inputRef = useRef(null);                                   // Phase 2
```

Mechanically: `useState` is a managed reference - like every cp3 reference-type value it lives on the heap behind a refcounted handle (`README.md` §10 does this for free, no special-casing needed in `cp3ui`); the setter marks the owning component dirty and schedules a reconcile on the logic thread.

## 7. Styling - runtime style objects, no CSS engine

This is the deliberate answer to a real Tailwind JIT failure mode: a build-time CSS scanner reading source as raw text cannot see a runtime value - `` className={`h-[${height}px]`} `` genuinely does not generate CSS when `height` is a state variable, since the JIT scanner has no way to know what the resulting string will be at build time. `cp3ui` never has that problem because styles are **plain cp3 objects evaluated at runtime**:

```jsx
<Box style={{
  height: isActive ? 100 : 200,          // ternary - works
  width: 100 + (count * 10),             // native math - works
  background: theme[`${color}-500`],     // runtime lookup - works
  padding: 16,
  borderRadius: 8
}}>
```

The reconciler diffs the style object; changed properties cross the queue as raw numbers/colors - never strings the renderer has to parse.

Supported property groups (Phase 1):

- **Layout:** `width`, `height`, `minWidth`, `maxWidth`, `minHeight`, `maxHeight`, `padding` (+ per-side), `margin` (+ per-side), `flexDirection`, `justifyContent`, `alignItems`, `gap`, `flex`
- **Visual:** `background`, `color`, `borderColor`, `borderWidth`, `borderRadius`, `opacity`
- **Text:** `fontSize`, `fontWeight`, `fontStyle`, `textAlign`, `lineHeight`

**Layout maps to Slint's native layouts** - `flexDirection: "column"` compiles to Slint's `VerticalLayout`, `"row"` to `HorizontalLayout`. This keeps Slint's reactive window-resize behavior for free rather than computing coordinates at runtime in cp3.

`StyleSheet.create` is supported for shared/named styles:

```cp3
const styles = StyleSheet.create({
  card: { padding: 20, background: "#ffffff", borderRadius: 12 },
  title: { fontSize: 24, fontWeight: "bold" }
});
```

**Tailwind-style `className` is an optional, Phase 2 build-time convenience, not a runtime system:** if a developer writes `className="bg-red-500 p-4"`, `transform-cpx` maps it to style-object properties at build time using a bundled utility dictionary. Dynamic values always go through style objects - that's the rule that keeps Tailwind's JIT failure mode impossible here, and it's also true of `className` itself: since the mapping happens at build time against literal class-name strings, a *dynamic* class name (`` className={`bg-${color}-500`} ``) is not supported and isn't meant to be - the fix for a dynamic value is always a style object, not a dynamic class name, same as the height example above.

## 8. Animations - GPU interpolation, Framer-Motion-class feel

Declarative transitions compile to Slint's native `animate` blocks:

```jsx
<Box style={{
  height: isActive ? 100 : 200,
  background: isActive ? "#3b82f6" : "#ef4444",
  transition: { duration: 300, easing: "ease-in-out" }
}} />
```

When the reconciler pushes the new property values over the queue, Slint's animation engine interpolates **on the UI thread/GPU** - meaning a heavy `fetch` or JSON parse on the logic thread cannot stutter an in-flight transition. This is the same property CSS transitions give a browser, achieved here without a browser. Declarative transitions ship in Phase 2, alongside the rest of the expanded event/hook set.

Physics/gesture-driven animation (drag, spring, fling) is a Phase 3 item: the logic thread computes the trajectory frame-by-frame (no GC pauses - cp3 guarantees smoothness here) and streams position updates through queue A. Specced here because it's the eventual Framer Motion use case; implemented after the declarative layer (Phase 2) ships and proves out.

## 9. Full usage example

```jsx
// src/App.cpx
import { Box, Text, Button, Input, useState } from "cp3ui";

export function App() {
  let [name, setName] = useState("");
  let [count, setCount] = useState(0);

  let heavyWork = async () => {
    let res = await fetch("https://api.example.com/data");  // logic thread - UI never stalls
    let data = await res.json();
    setName(data.name);
  };

  return (
    <Window title="My cp3ui App" width={600} height={400}>
      <Box style={{ padding: 40, flexDirection: "column", gap: 16 }}>
        <Text style={{ fontSize: 32, fontWeight: "bold" }}>
          Hello {name || "World"}!
        </Text>
        <Input value={name} placeholder="Your name"
               onChange={(v) => setName(v)} />
        <Button style={{ background: "#007acc", color: "#fff", padding: 12,
                         borderRadius: 8 }}
                onClick={heavyWork}>
          Fetched {count} times
        </Button>
      </Box>
    </Window>
  );
}
```
```cp3
// src/main.cp3
import { render } from "cp3ui";
import { App } from "./App.cpx";

render(() => App());
```

## 10. Package structure

```
cp3ui/
├── package.json                  # "type": "package", "bin", cacheBinary for the bridge
├── src/
│   ├── index.cp3                 # public API: components, hooks, render (the "main" entry)
│   ├── cli.cp3                   # the runnable tool entry point (the "bin" entry) - dispatches to transform-cpx
│   ├── transform-cpx.cp3         # the build-time JSX transformer, invoked by cli.cp3
│   ├── reconciler.cp3            # virtual tree, diffing, commit phase
│   ├── hooks.cp3                 # useState (Phase 1); useEffect/useMemo/useCallback/useRef (Phase 2)
│   ├── stylesheet.cp3            # StyleSheet.create + style resolution
│   ├── events.cp3                # synthetic event system + bubbling (Phase 2)
│   └── tailwind-map.cp3          # optional className → style dictionary (Phase 2)
├── bridge/                       # hand-written C++, statically linked
│   ├── cp3ui_bridge.cpp/.hpp     # C-style API over two Boost.Lockfree queues
│   ├── slint_backend.cpp         # Slint component management, thread ownership
│   └── CMakeLists.txt            # builds Slint's C++ API into the bridge
└── README.md
```

`package.json`:

```json
{
  "name": "cp3ui",
  "version": "0.1.0",
  "type": "package",
  "main": "src/index.cp3",
  "bin": { "cp3ui": "src/cli.cp3" },
  "build": {
    "cacheBinary": ["windows-x64", "linux-x64", "mac-arm64"]
  }
}
```

`"main"` vs `"bin"`, stated plainly since they're easy to conflate: `"main"` is the library surface a consumer `import`s (`Box`, `Text`, `useState`, `render`, ...). `"bin"` is a separate, ordinary cp3 *program* a consumer *runs* via `cp3 exec cp3ui <args>` (§11) - it never gets `import`ed, and `index.cp3` never references it.

`cacheBinary` matters here specifically: the Slint bridge is real C++ that consumers should not have to compile per install. The cp3 side ships as source (per `.cp3pak` rules, `CLI-README.md` §5.3); the bridge ships precompiled per platform with source fallback, exactly like any other native-backed package.

## 11. Running `cp3ui`'s tools - `"bin"` + `cp3 exec`

This section exists because a real gap was found and closed during design, and it's worth stating the gap plainly rather than presenting the fix as if it were obvious from the start: a `.cp3pak` (`CLI-README.md` §5) is fundamentally a **library** - there was originally no way for a package to ship something *runnable*. That breaks `cp3ui`, because `transform-cpx` (§3) has to actually run before `cp3 build` starts. The fix - an npm-`bin`-shaped `"bin"` field plus a `cp3 exec` command - is now part of `CLI-README.md` itself (§5.1, §1), not something specific to this package. This section is `cp3ui`'s concrete usage of that general mechanism.

**Beta one, the only path `cp3ui` actually needs:**

```bash
cp3 install cp3ui                 # ordinary dependency install (CLI-README.md §4)
```
```json
{
  "scripts": {
    "prebuild": "cp3 exec cp3ui transform",
    "build": "cp3 build"
  }
}
```

Running `cp3 build` (or `npm run build`) runs `prebuild` first automatically (`CLI-README.md` §1's lifecycle-hook confirmation), which runs `cp3 exec cp3ui transform`:

1. `cp3 exec` resolves the already-installed `cp3ui` package and reads its `"bin"` entry - `src/cli.cp3`.
2. Builds that one file into an executable (reusing `cp3 run`'s existing build machinery - nothing new).
3. Caches the result in `.cp3/packages/cp3ui/.bin/`, so only the first build after a `cp3ui` version change pays the compile cost.
4. Runs it with `transform` as `process.argv`, which dispatches into `transform-cpx.cp3` and does the `.cpx` → `.cp3` conversion described in §3.

**What this does *not* require:** a package-is-an-executable special case, a new `.cp3pak` variant, or any change to how `cp3 build` itself works. `cp3 exec` is what makes a library package able to expose a tool at all - `cp3ui` is simply the first real consumer of it.

**Left for later, not needed by `cp3ui`:** the `npx`-style fetch-and-run-without-installing form (`cp3 exec github:<user>/<repo> init`) is a `CLI-README.md` Phase 2 roadmap item, not something `cp3ui` depends on - by the time its `prebuild` script runs, `cp3ui` is always already an ordinary installed dependency.

## 12. Roadmap

**Phase 1 - v0.1 (ship thin, prove the pipeline end-to-end)**
`.cpx` transform + source maps, wired through `cp3 exec` (§3, §11) · `Window`/`Box`/`Text`/`Button`/`Input` · `useState` · style objects (layout + visual + text groups, §7) · `onClick`/`onChange`/`onFocus`/`onBlur` (direct, non-bubbling) · two-thread bridge (§4) · vsync frame pacing (§5).

**Phase 2 - v0.5**
Full event set + synthetic bubbling · `useEffect`/`useMemo`/`useCallback`/`useRef` · `TextArea`/`Image`/`ScrollView`/`List`/`Modal`/form controls (`Checkbox`/`Radio`/`Slider`/`Select`) · declarative `transition` animations · Tailwind `className` mapping · a `cp3ui init` scaffolding command built on `cp3 exec`.

**Phase 3 - v1.0**
Physics/gesture-driven animations · high-refresh tuning (240Hz verification on real hardware) · DevTools (component inspector, state viewer) · accessibility tree · Yoga layout-engine integration, but only if Slint's native layouts prove insufficient in practice - not adopted preemptively.

## 13. What `cp3ui` is NOT

- Not part of the compiler or the stdlib - an ordinary third-party `.cp3pak`.
- Not a web framework - renders to native desktop windows, never a browser tab.
- Not webview-based - webview was rejected for this project, everywhere it came up (§1).
- Not a Qt wrapper - Qt was evaluated and rejected for `cp3ui` specifically (§1); the project's separate, real Qt-bridge plan (`CLI-README.md`'s roadmap) is unrelated and doesn't feed into this package.
- Not a claim that every component/hook/event in §6-§8 exists on day one - availability is phased, and the tables say which phase.

## 14. Open questions

- [ ] HiDPI / per-monitor DPI scaling edge cases across the three target OSes - needs real hardware testing, not paper design.
- [ ] Text shaping for complex scripts (RTL, emoji, ligatures) - Slint handles much of this already; verify coverage before promising full parity.
- [ ] Native system dialogs (file picker, message box) - Slint has no complete story here; may need small per-OS bridge additions.
- [ ] Exact minimum Skia feature set to bundle, to keep the 10-20 MB idle-RAM target (§1) honest in practice, not just in theory.
- [ ] Hot-reload behavior in `cp3 dev` mode - re-rendering the tree on file save without losing component state is desirable but not yet designed; not required for Phase 1.
