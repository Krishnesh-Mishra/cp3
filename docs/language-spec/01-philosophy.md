# 1. Philosophy

C+++ aims to combine:

- **JavaScript/TypeScript-like syntax** — familiar to the largest pool of developers
- **Static typing** — no `any`
- **Native executable output** — real performance, not a runtime/VM
- **Automatic memory management** — no manual `new`/`delete`, no raw pointers for normal users
- **C/C++ interoperability** — access to the existing native ecosystem (Qt, Unreal, OpenCV, etc.)
- **Modern tooling** — built-in package manager, simple modules, no header files

The core bet: most of C++'s complexity (manual memory, pointers, headers, build systems) is *incidental*, not essential to getting native performance. Hide it behind a friendlier language and a smart compiler.

**cp3 is a hosted language, not a freestanding/systems one — worth stating explicitly, since "compiles to native code" invites the question.** Every program cp3 produces assumes a real OS underneath it (libuv needs `epoll`/`kqueue`/IOCP from the kernel; the managed heap needs a working allocator; `import native` needs a real dynamic linker). That's a deliberate scope boundary, not a gap: writing an OS kernel needs the opposite of everything cp3 provides — no runtime, no heap allocator handed to you, no async event loop, raw hardware/interrupt access, and the raw pointers and inline assembly this spec deliberately keeps out of normal cp3 code ([§12](12-pointers-references.md), [§14](14-async.md)). None of that is on the beta-one roadmap, and doing it properly would mean a genuinely separate freestanding compilation target, not an extension of this one. cp3's actual target — and where it has a real advantage over the incumbents — is **hosted userspace software**: web servers, CLI tools, desktop apps, daemons, shells, terminal emulators — the exact space Node/Electron/Go/Python occupy today, not the space C/Rust/Zig occupy for kernel work.

