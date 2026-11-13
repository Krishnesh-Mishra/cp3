// dist/extensions/cp3-vscode/src/intelligence/stdlibCatalog.ts
//
// The closed cp3 standard-library surface, as documented in
// docs/content/03-standard-library. The compiler diagnoses members outside
// these tables, so this catalog only offers names the language accepts.
//
// `importStyle` mirrors how the module is meant to be imported:
//   - "default": `import xs from "std/xs";` - the binding is a namespace
//     object whose members are offered after `xs.`
//   - "named":   `import { a, b } from "std/xs";` - the named exports are
//     offered at identifier position with an auto-import edit
//
// Every entry lists the documented member name, a signature, a one-line doc
// and the kind VS Code should show. Signatures are copied from the docs
// tables; where a doc only gives a rule ("which members are offered is closed"),
// the catalog sticks to what is documented and nothing else.

export type StdlibMemberKind = "function" | "constant" | "property";

export interface StdlibMember {
    readonly name: string;
    readonly signature: string;
    readonly doc: string;
    readonly kind: StdlibMemberKind;
}

export interface StdlibModule {
    /** Module specifier, e.g. "std/math". */
    readonly specifier: string;
    readonly importStyle: "default" | "named";
    /** The conventional binding name from the docs; any local alias works. */
    readonly defaultName: string;
    readonly summary: string;
    readonly members: readonly StdlibMember[];
    /** Extra aliases the parser should treat as the same module (case-insensitive). */
    readonly aliases?: readonly string[];
}

const fn = (name: string, signature: string, doc: string): StdlibMember =>
    ({ name, signature, doc, kind: "function" });
const constant = (name: string, doc: string): StdlibMember =>
    ({ name, signature: `${name}: number`, doc, kind: "constant" });
const property = (name: string, signature: string, doc: string): StdlibMember =>
    ({ name, signature, doc, kind: "property" });

// std/math — docs: 07-math.md
const mathMembers: readonly StdlibMember[] = [
    constant("PI", "Ratio of a circle's circumference to its diameter."),
    constant("E", "Euler's number, the base of natural logarithms."),
    constant("LN2", "Natural logarithm of 2."),
    constant("LN10", "Natural logarithm of 10."),
    constant("LOG2E", "Base-2 logarithm of Euler's number."),
    constant("LOG10E", "Base-10 logarithm of Euler's number."),
    constant("SQRT1_2", "Square root of one half."),
    constant("SQRT2", "Square root of 2."),
    fn("random", "random(): number", "Pseudo-random double in [0, 1). Not cryptographic; use std/crypto."),
    fn("min", "min(...values: number[]): number", "Smallest of the supplied values."),
    fn("max", "max(...values: number[]): number", "Largest of the supplied values."),
    fn("pow", "pow(base: number, exponent: number): number", "Exponentiation."),
    fn("gcd", "gcd(a: number, b: number): number", "Greatest common divisor of exactly representable integers."),
    fn("lcm", "lcm(a: number, b: number): number", "Least common multiple of exactly representable integers."),
    fn("clamp", "clamp(x: number, min: number, max: number): number", "Clamp x to the [min, max] range (lower bound first)."),
    fn("lerp", "lerp(start: number, end: number, t: number): number", "Linear interpolation; t outside [0, 1] extrapolates."),
    fn("sqrt", "sqrt(x: number): number", "Square root."),
    fn("abs", "abs(x: number): number", "Absolute value."),
    fn("floor", "floor(x: number): number", "Largest integer not greater than x."),
    fn("ceil", "ceil(x: number): number", "Smallest integer not smaller than x."),
    fn("round", "round(x: number): number", "Round to nearest integer (runtime rounding)."),
    fn("trunc", "trunc(x: number): number", "Remove the fractional part toward zero."),
    fn("cbrt", "cbrt(x: number): number", "Cube root."),
    fn("log", "log(x: number): number", "Natural logarithm."),
    fn("log2", "log2(x: number): number", "Base-2 logarithm."),
    fn("log10", "log10(x: number): number", "Base-10 logarithm."),
    fn("sin", "sin(x: number): number", "Sine (radians)."),
    fn("cos", "cos(x: number): number", "Cosine (radians)."),
    fn("tan", "tan(x: number): number", "Tangent (radians)."),
    fn("asin", "asin(x: number): number", "Arcsine (radians); expects [-1, 1]."),
    fn("acos", "acos(x: number): number", "Arccosine (radians); expects [-1, 1]."),
    fn("atan", "atan(x: number): number", "Arctangent (radians)."),
    fn("atan2", "atan2(y: number, x: number): number", "Angle of the ray from (0, 0) to (x, y), in radians."),
    fn("sinh", "sinh(x: number): number", "Hyperbolic sine."),
    fn("cosh", "cosh(x: number): number", "Hyperbolic cosine."),
    fn("tanh", "tanh(x: number): number", "Hyperbolic tangent."),
    fn("asinh", "asinh(x: number): number", "Inverse hyperbolic sine."),
    fn("acosh", "acosh(x: number): number", "Inverse hyperbolic cosine."),
    fn("atanh", "atanh(x: number): number", "Inverse hyperbolic tangent; expects (-1, 1)."),
    fn("exp", "exp(x: number): number", "Euler's number raised to the power x."),
    fn("log1p", "log1p(x: number): number", "Natural logarithm of 1 + x, accurate for small x."),
    fn("hypot", "hypot(...values: number[]): number", "Square root of the sum of squares of the arguments."),
    fn("sign", "sign(x: number): number", "-1, 0 or 1 according to the sign of x; NaN stays NaN."),
];

// std/console — docs: 01-console.md
const consoleMembers: readonly StdlibMember[] = [
    fn("log", "log(...args): void", "Write to stdout with a newline."),
    fn("info", "info(...args): void", "Write to stdout with a newline (naming convenience)."),
    fn("warn", "warn(...args): void", "Write to stderr with a newline."),
    fn("error", "error(...args): void", "Write to stderr with a newline."),
    fn("assert", "assert(condition: unknown, ...args): void", "Write to stderr only when condition is falsy."),
    fn("count", "count(label?: string): void", "Print `label: N` and increment the per-label counter (default `default`)."),
    fn("countReset", "countReset(label?: string): void", "Reset the counter for `label` (default `default`)."),
    fn("dir", "dir(value: unknown): void", "Print a value like log (objects in field order)."),
    fn("trace", "trace(...args): void", "Print a `Trace:` line with the arguments (no stack trace)."),
    fn("group", "group(...args): void", "Print the arguments, then indent following stdout lines by two spaces."),
    fn("groupEnd", "groupEnd(): void", "Close the innermost open group (never below zero indent)."),
    fn("table", "table(rows: unknown): void", "Print an (index) (value) listing for an array, or key value lines for an object literal."),
    fn("input", "input(prompt?: string): Promise<string>", "Read one line from stdin; EOF resolves to an empty string."),
    fn("readKey", "readKey(): Promise<string>", "Read one keypress; raw mode while pending."),
    fn("on", "on(event: \"keypress\" | \"line\" | \"close\", listener: (value: string) => void): void", "Register a stdin listener; stays until removed."),
    fn("off", "off(event: \"keypress\" | \"line\" | \"close\", listener: (value: string) => void): void", "Remove the exact listener registered for the event."),
    fn("clear", "clear(): void", "Clear the terminal screen."),
    fn("hide", "hide(): void", "Hide the console window (Windows; no-op elsewhere)."),
    fn("show", "show(): void", "Restore the console window (Windows; no-op elsewhere)."),
    fn("time", "time(label?: string): void", "Record the start instant for `label` (default `default`)."),
    fn("timeEnd", "timeEnd(label?: string): void", "Print `label: <ms>ms` and remove the timer (default `default`)."),
];

// std/process — docs: 03-process.md
const processMembers: readonly StdlibMember[] = [
    property("argv", "argv: string[]", "Arguments passed to this program."),
    property("env", "env: Map<string, string>", "The program's in-memory environment map."),
    fn("exit", "exit(code: number): void", "Flush stdout and terminate with a numeric exit code."),
    fn("exec", "exec(cmd: string, args: string[]): Promise<ExecResult>", "Run a child and collect stdout/stderr until it exits."),
    fn("spawn", "spawn(cmd: string, args: string[]): SpawnHandle", "Start a child and observe output/exit events while it runs."),
    fn("cwd", "cwd(): string", "Current working directory."),
    fn("chdir", "chdir(path: string): void", "Change the working directory; throws on failure."),
    property("pid", "pid: number", "Process id of this process."),
    property("ppid", "ppid: number", "Process id of the parent process."),
    fn("uptime", "uptime(): number", "Seconds since the program started (monotonic)."),
    fn("memoryUsage", "memoryUsage(): MemoryUsage", "Resident-set size; the heap fields are always 0."),
    fn("hrtime", "hrtime(): [number, number]", "[seconds, nanoseconds] since an unspecified monotonic origin."),
    property("platform", "platform: string", "OS platform: \"win32\", \"darwin\" or \"linux\"."),
    property("arch", "arch: string", "CPU architecture: \"x64\" or \"arm64\"."),
    property("version", "version: string", "The CP3 runtime version string."),
    property("exitCode", "exitCode: number", "Stored exit code (default 0); assignment does not exit."),
    fn("nextTick", "nextTick(callback: () => void): void", "Queue callback before timers and promise continuations (FIFO)."),
    property("stdout", "stdout: StdioStream", "Writable stream for stdout: .write(s) emits s with no newline, then flushes."),
    property("stderr", "stderr: StdioStream", "Writable stream for stderr; same shape as stdout."),
    property("stdin", "stdin: StdinStream", "Readable stream: readAll / readLines / readLine / on(\"line\" | \"close\") / close()."),
    fn("on", "on(event: \"SIGINT\" | \"SIGTERM\" | \"exit\" | \"unhandledRejection\", listener: (value: unknown) => void): void", "Register a lifecycle listener."),
    fn("onShutdown", "onShutdown(listener: () => void): void", "Sugar for on(\"exit\", ...); runs once before the process ends."),
];

// std/fs — docs: 04-fs.md
const fsMembers: readonly StdlibMember[] = [
    fn("readFileSync", "readFileSync(path: string): string", "Read a file synchronously; contents are always returned as a string."),
    fn("readFile", "readFile(path: string): Promise<string>", "Read a file (promise); contents are always resolved as a string."),
    fn("writeFileSync", "writeFileSync(path: string, data: string): void", "Write a file synchronously, truncating it."),
    fn("writeFile", "writeFile(path: string, data: string): Promise<void>", "Write a file (promise), truncating it."),
    fn("appendFileSync", "appendFileSync(path: string, data: string): void", "Append to a file synchronously."),
    fn("appendFile", "appendFile(path: string, data: string): Promise<void>", "Append to a file (promise)."),
    fn("existsSync", "existsSync(path: string): boolean", "Point-in-time check; another process can change it immediately."),
    fn("mkdirSync", "mkdirSync(path: string): void", "Create a directory synchronously."),
    fn("mkdir", "mkdir(path: string): Promise<void>", "Create a directory (promise)."),
    fn("readdirSync", "readdirSync(path: string): string[]", "List directory entries synchronously."),
    fn("readdir", "readdir(path: string): Promise<string[]>", "List directory entries (promise)."),
    fn("statSync", "statSync(path: string): FileStats", "File metadata snapshot: size, isFile, isDirectory, modifiedAt."),
    fn("stat", "stat(path: string): Promise<FileStats>", "File metadata snapshot (promise): size, isFile, isDirectory, modifiedAt."),
    fn("unlinkSync", "unlinkSync(path: string): void", "Remove a file synchronously; no directory variant exists."),
    fn("unlink", "unlink(path: string): Promise<void>", "Remove a file (promise); no directory variant exists."),
    fn("readFileStream", "readFileStream(path: string, chunkSize?: number): ReadStream", "Yield Buffer chunks; see the ReadStream docs for on(\"data\" | \"end\" | \"error\")."),
    fn("writeFileStream", "writeFileStream(path: string): WriteStream", "WriteStream whose write accepts string | Buffer."),
    fn("watch", "watch(path: string, callback: (eventType: string, filename: string) => void): FsWatcher", "Watch a file or directory; callback(eventType, filename); close() stops delivery."),
];

// std/path — docs: 05-path.md
const pathMembers: readonly StdlibMember[] = [
    property("sep", "sep: string", "Platform path separator."),
    fn("join", "join(...parts: string[]): string", "Join path segments."),
    fn("resolve", "resolve(...parts: string[]): string", "Resolve segments to an absolute path."),
    fn("extname", "extname(path: string): string", "Filename extension including the dot."),
    fn("dirname", "dirname(path: string): string", "Directory portion of a path."),
    fn("normalize", "normalize(path: string): string", "Normalize separators and dot segments."),
    fn("basename", "basename(path: string, extension?: string): string", "Final path segment; optional extension is stripped from the result."),
    fn("isAbsolute", "isAbsolute(path: string): boolean", "Whether the path is absolute on this platform."),
];

// std/os — docs: 06-os.md
const osMembers: readonly StdlibMember[] = [
    fn("platform", "platform(): string", "Platform name."),
    fn("hostname", "hostname(): string", "Machine host name."),
    fn("homedir", "homedir(): string", "Current user's home directory."),
    fn("tmpdir", "tmpdir(): string", "System temporary directory."),
    fn("cpus", "cpus(): number", "Logical CPU count."),
    fn("totalMemory", "totalMemory(): number", "Total memory in bytes."),
    fn("freeMemory", "freeMemory(): number", "Free memory in bytes."),
    fn("totalmem", "totalmem(): number", "Node alias of totalMemory()."),
    fn("freemem", "freemem(): number", "Node alias of freeMemory()."),
    fn("loadavg", "loadavg(): [number, number, number]", "1/5/15 minute load averages."),
    fn("networkInterfaces", "networkInterfaces(): Array<[string, string, string, boolean]>", "One [name, address, family, internal] tuple per interface."),
];

// std/date — docs: 08-date.md (namespace operations; instance members are Date's own)
const dateMembers: readonly StdlibMember[] = [
    fn("now", "now(): i64", "Milliseconds since the epoch."),
    fn("fromISOString", "fromISOString(s: string): Date", "Parse an ISO-8601 string; treat untrusted input as untrusted."),
    fn("parse", "parse(s: string): i64", "Milliseconds since the epoch, or throws/returns an error-sentinel per vocabulary."),
];

// std/json — docs: 09-json.md
const jsonMembers: readonly StdlibMember[] = [
    fn("parse", "parse<T>(text: string): T", "Parse JSON; generic parameter expresses the runtime validation contract."),
    fn("parseFileSync", "parseFileSync<T>(path: string): T", "Read and parse a file exactly like JSON.parse<T>."),
    fn("parseFile", "parseFile<T>(path: string): Promise<T>", "Read and parse a file asynchronously."),
    fn("stringify", "stringify(value: unknown, replacer?: (key: string, value: unknown) => unknown, space?: number | string): string", "Serialize to JSON; no reviver is exposed."),
    fn("stringifyToFileSync", "stringifyToFileSync(path: string, value: unknown): void", "Serialize and overwrite a file with exactly what stringify would produce."),
    fn("stringifyToFile", "stringifyToFile(path: string, value: unknown): Promise<void>", "Serialize and overwrite a file asynchronously."),
    fn("verify", "verify<T>(text: string): boolean", "True iff text parses as JSON of type T."),
];

// std/crypto — docs: 10-crypto.md
const cryptoMembers: readonly StdlibMember[] = [
    fn("randomUUID", "randomUUID(): string", "RFC 4122 version-4 UUID text."),
    fn("randomBytes", "randomBytes(length: number): Buffer", "Cryptographically random bytes; size capped at 2,147,483,647."),
    fn("getRandomValues", "getRandomValues(length: number): Buffer", "Alias of randomBytes with the same length rules."),
    fn("sha256", "sha256(data: string | Buffer): string", "Lowercase hexadecimal SHA-256 digest."),
    fn("sha512", "sha512(data: string | Buffer): string", "Lowercase hexadecimal SHA-512 digest."),
    fn("sha256Raw", "sha256Raw(data: string | Buffer): Buffer", "Raw 32 SHA-256 digest bytes."),
    fn("hmacSha256", "hmacSha256(key: string | Buffer, data: string | Buffer): string", "Lowercase hexadecimal HMAC-SHA-256."),
    fn("hmacSha512", "hmacSha512(key: string | Buffer, data: string | Buffer): string", "Lowercase hexadecimal HMAC-SHA-512."),
    fn("blake2b", "blake2b(data: string | Buffer): string", "Lowercase hexadecimal BLAKE2b digest (unkeyed, 32 bytes)."),
    fn("AES256", "AES256(key: string, data: string): string", "AES-256-GCM envelope as base64 text (fresh nonce per call)."),
    fn("AES256Decrypt", "AES256Decrypt(key: string, encrypted: string): string", "Authenticated AES-256-GCM plaintext; throws on failure."),
    fn("uuidParse", "uuidParse(uuid: string): Buffer", "16 raw bytes of a canonical UUID string; throws on malformed input."),
    fn("uuidValidate", "uuidValidate(uuid: string): boolean", "True iff uuid is a canonical UUID string (format only)."),
    fn("uuidVersion", "uuidVersion(uuid: string): number", "Version nibble (0-15) of a canonical UUID string; 0 when not canonical."),
];

// std/time — docs: 21-time.md
const timeMembers: readonly StdlibMember[] = [
    fn("stopwatch", "stopwatch(): Stopwatch", "Create a new stopped Stopwatch; new Stopwatch() is not part of the surface."),
];

// std/readline — docs: 22-readline.md
const readlineMembers: readonly StdlibMember[] = [
    fn("readLine", "readLine(): Promise<string>", "Next line of stdin (terminator removed); \"\" on EOF."),
    fn("readLines", "readLines(): Promise<string[]>", "All remaining lines to EOF, terminators removed."),
    fn("readAll", "readAll(): Promise<string>", "All remaining stdin bytes as one string; never rejects."),
    fn("createReader", "createReader(): LineReader", "A line reader over stdin: readLine/readLines/on(\"line\" | \"close\")/close."),
];

// std/hash — docs: 23-hash.md
const hashMembers: readonly StdlibMember[] = [
    fn("crc32", "crc32(input: string | Buffer): u32", "IEEE 802.3 CRC-32 (number in TypeScript projects)."),
    fn("crc32c", "crc32c(input: string | Buffer): u32", "Castagnoli CRC-32C."),
    fn("fnv1a32", "fnv1a32(input: string | Buffer): u32", "FNV-1a 32-bit."),
    fn("fnv1a64", "fnv1a64(input: string | Buffer): u64", "FNV-1a 64-bit."),
    fn("xxhash64", "xxhash64(input: string | Buffer): u64", "XXH64 with the seed fixed to 0."),
];

// std/io — docs: 18-io.md
const ioMembers: readonly StdlibMember[] = [
    fn("waitReadable", "waitReadable(fd: number): Promise<undefined>", "Await until fd is readable."),
    fn("waitWritable", "waitWritable(fd: number): Promise<undefined>", "Await until fd is writable."),
    fn("pollUntilDone", "pollUntilDone(...): Promise<undefined>", "Drive a getSocket()/checkProgress poll loop to completion."),
    constant("POLL_READING", "checkProgress verdict: await waitReadable(getSocket())."),
    constant("POLL_WRITING", "checkProgress verdict: await waitWritable(getSocket())."),
];

// std/env — docs: 02-env.md (named export `env`)
const envMembers: readonly StdlibMember[] = [
    fn("get", "get<T>(key: string, options?: { default?: T }): T", "Look up a key; throws when missing without a default."),
    fn("has", "has(key: string): boolean", "True iff the key exists; empty-string OS values count as absent."),
    fn("load", "load(path: string): void", "Load a .env file into the environment; throws if unreadable."),
];

// std/url — docs: 19-url.md (named exports: URL, URLSearchParams + encoding helpers)
const urlMembers: readonly StdlibMember[] = [
    fn("encodeURIComponent", "encodeURIComponent(s: string): string", "Component percent-encoding."),
    fn("decodeURIComponent", "decodeURIComponent(s: string): string", "Component percent-decoding; malformed sequences are left as-is."),
    fn("encodeURI", "encodeURI(s: string): string", "Whole-URI percent-encoding."),
    fn("decodeURI", "decodeURI(s: string): string", "Whole-URI percent-decoding."),
    fn("URL", "new URL(input: string, base?: string): URL", "Parse/resolve with RFC 3986 rules."),
    fn("URLSearchParams", "new URLSearchParams(query?: string): URLSearchParams", "Parse/manipulate a query string."),
];

// std/text — docs: 20-text.md (named exports: TextEncoder, TextDecoder)
const textMembers: readonly StdlibMember[] = [
    fn("TextEncoder", "new TextEncoder(encoding?: string): TextEncoder", "UTF-8 encoder; only utf-8/utf8 labels are accepted."),
    fn("TextDecoder", "new TextDecoder(encoding?: string): TextDecoder", "UTF-8 decoder; malformed bytes become U+FFFD, a leading BOM is stripped."),
];

// std/buffer — docs: 11-buffer.md (named export `Buffer`)
const bufferMembers: readonly StdlibMember[] = [
    fn("Buffer", "new Buffer(length: number): Buffer", "Byte buffer; slice() copies, Buffer.alloc/from forms per docs."),
];

// std/collections — docs: 12-collections.md (named exports; classes imported individually)
const collectionsMembers: readonly StdlibMember[] = [
    fn("Map", "new Map<K, V>(): Map<K, V>", "Insertion-ordered key/value map; static Map.groupBy(list, callback) groups an array into Map<K, T[]>."),
    fn("Set", "new Set<T>(): Set<T>", "Insertion-ordered set; union/intersection/difference/isSubsetOf return fresh Sets."),
    fn("LRUCache", "new LRUCache<K, V>(capacity: number): LRUCache<K, V>", "Least-recently-used cache with fixed capacity."),
    fn("Queue", "new Queue<T>(): Queue<T>", "FIFO queue; enqueue/dequeue/peek/isEmpty."),
    fn("Stack", "new Stack<T>(): Stack<T>", "LIFO stack; push/pop/peek/isEmpty."),
    fn("PriorityQueue", "new PriorityQueue<T>(comparator?: (a: T, b: T) => number): PriorityQueue<T>", "Min-priority queue; pop throws when empty."),
    fn("RingBuffer", "new RingBuffer<T>(capacity: number): RingBuffer<T>", "Fixed-capacity ring buffer; isFull, capacity."),
    fn("BitSet", "new BitSet(size?: number): BitSet", "Growing bit set; and/or/xor return fresh sets."),
    fn("MultiMap", "new MultiMap<K, V>(): MultiMap<K, V>", "Map from K to V[]."),
    fn("ConcurrentMap", "new ConcurrentMap<K, V>(): ConcurrentMap<K, V>", "Thread-safe map; no cross-thread ordering guarantee."),
];

export const STDLIB_MODULES: readonly StdlibModule[] = [
    { specifier: "std/math", importStyle: "default", defaultName: "Math", aliases: ["math"], summary: "Numeric constants and common floating-point functions.", members: mathMembers },
    { specifier: "std/console", importStyle: "default", defaultName: "console", summary: "Terminal output, input helpers and stdin events.", members: consoleMembers },
    { specifier: "std/process", importStyle: "default", defaultName: "process", summary: "Arguments, environment map, child processes, stdio and lifecycle signals.", members: processMembers },
    { specifier: "std/fs", importStyle: "default", defaultName: "fs", summary: "Synchronous, promise and streaming file operations.", members: fsMembers },
    { specifier: "std/path", importStyle: "default", defaultName: "path", summary: "Platform path manipulation.", members: pathMembers },
    { specifier: "std/os", importStyle: "default", defaultName: "os", summary: "Host and machine information.", members: osMembers },
    { specifier: "std/date", importStyle: "default", defaultName: "Date", summary: "Namespace operations; instance methods live on Date values.", members: dateMembers },
    { specifier: "std/json", importStyle: "default", defaultName: "JSON", summary: "JSON serialization and checked parsing helpers.", members: jsonMembers },
    { specifier: "std/crypto", importStyle: "default", defaultName: "crypto", summary: "Random bytes, hashes, UUIDs and AES-256 encryption.", members: cryptoMembers },
    { specifier: "std/time", importStyle: "default", defaultName: "time", summary: "Monotonic Stopwatch factory and console timers.", members: timeMembers },
    { specifier: "std/readline", importStyle: "default", defaultName: "readline", summary: "Line reading over stdin.", members: readlineMembers },
    { specifier: "std/hash", importStyle: "default", defaultName: "hash", summary: "Non-crypto crc32/crc32c/fnv1a32/fnv1a64/xxhash64.", members: hashMembers },
    { specifier: "std/io", importStyle: "default", defaultName: "io", summary: "Non-blocking fd readiness and pollUntilDone.", members: ioMembers },
    { specifier: "std/env", importStyle: "named", defaultName: "env", summary: "Environment lookup and .env loading.", members: envMembers },
    { specifier: "std/url", importStyle: "named", defaultName: "URL", summary: "URL and URLSearchParams classes and percent encoding.", members: urlMembers, aliases: ["URL"] },
    { specifier: "std/text", importStyle: "named", defaultName: "TextEncoder", summary: "UTF-8 TextEncoder/TextDecoder.", members: textMembers, aliases: ["TextEncoder", "TextDecoder"] },
    { specifier: "std/buffer", importStyle: "named", defaultName: "Buffer", summary: "Byte buffers and text encodings.", members: bufferMembers, aliases: ["Buffer"] },
    { specifier: "std/collections", importStyle: "named", defaultName: "Map", summary: "Map, Set, LRUCache, Queue, Stack, PriorityQueue, RingBuffer, BitSet, MultiMap, ConcurrentMap.", members: collectionsMembers, aliases: ["Map", "Set"] },
];

/** Default-namespace modules keyed by their conventional binding name (case-insensitive). */
export const DEFAULT_MODULES: readonly StdlibModule[] = STDLIB_MODULES.filter((m) => m.importStyle === "default");

/** Named-import modules keyed by their exported name (case-insensitive). */
export const NAMED_EXPORTS: readonly StdlibModule[] = STDLIB_MODULES.filter((m) => m.importStyle === "named");

/** Looks up a stdlib module by the local binding name it was imported with. */
export function findDefaultModule(binding: string): StdlibModule | undefined {
    const wanted = binding.toLowerCase();
    for (const module of DEFAULT_MODULES) {
        if (module.defaultName.toLowerCase() === wanted) return module;
        for (const alias of module.aliases ?? []) {
            if (alias.toLowerCase() === wanted) return module;
        }
    }
    return undefined;
}
