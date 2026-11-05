// =============================================================================
// src/std/process/sema.cpp - `std/process` member typing.
//
// OWNERSHIP: the std/process family (src/std/process/**). Moved here from
// src/sema/src/checks/std_check.cpp (D72 stage 2); that core unit keeps the
// namespace dispatch (stdNamespaceMember) and forwards here, so the rest of
// sema never needs to change for this family.
//
// The typed surface is the documented set (STDLIB docs §04, process section):
// `argv`, `env`, `exit`, `exec`, `spawn`. `exec`/`spawn` resolve to the
// `$process:ExecResult` / `$process:SpawnHandle` marker shapes below, which
// codegen (src/std/process/codegen.cpp) recognises by name. The shapes are
// CLOSED: the docs pin them as interfaces with exactly these fields, so
// `result.stdout` types as the string it is (an ambient field would silently
// compare unequal and could not take `.length`; see member_check.cpp) and an
// undocumented member is CP3305 with the documented names as candidates.
//
// `process.env` is `Map<string, string>` (docs §04). The collections family
// (src/std/collections/sema.cpp) owns that type's member table, so
// `env.get(missing)` types `string | undefined` - the pinned
// process_env_get_unset equality - and every member lookup flows through the
// shared Map inference (member_check.cpp's Class branch).
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cp3/host/std_integer.hpp"
#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

const char* const kExecResult = "$process:ExecResult";
const char* const kSpawnHandle = "$process:SpawnHandle";
const char* const kMemoryUsage = "$process:MemoryUsage";
const char* const kStdioStream = "$process:StdioStream";

/// `(p0, p1, ...) => void` with permissive parameters: the documented event
/// listener shapes share one grammar and the parameters are typed
/// contextually by codegen (the runtime callable's declared types).
Type callbackOf(std::size_t arity) {
    return Type::function(std::vector<Type>(arity, ambient()), Type::voidType());
}

// The documented members of the `process` namespace
// (docs/standard-library/04-imported-std-modules.md, process section).
const DocRow kProcessRows[] = {
    {"argv", "argv: string[]",
     "The command-line arguments the program was started with."},
    {"env", "env: Map<string, string>",
     "The environment variables of the process, as a `Map` of names to "
     "values."},
    {"exit", "exit(code: number): void",
     "Ends the program with the exit code `code`."},
    {"exec", "exec(cmd: string, args: string[]): Promise<ExecResult>",
     "Runs `cmd` with `args` and resolves once the process has finished, with "
     "its `ExecResult`.\n\n"
     "A non-zero exit code does not reject the promise - check "
     "`result.exitCode` yourself. A rejection is reserved for a command that "
     "cannot be launched at all."},
    {"spawn", "spawn(cmd: string, args: string[]): SpawnHandle",
     "Runs `cmd` with `args` and returns a live `SpawnHandle` right away, so "
     "the output can be consumed as it arrives."},
    {"cwd", "cwd(): string",
     "The current working directory of the process."},
    {"chdir", "chdir(path: string): void",
     "Changes the current working directory to `path`. Throws an ordinary "
     "catchable error when the directory cannot be entered."},
    {"pid", "pid: number",
     "The process id of the current process."},
    {"ppid", "ppid: number",
     "The process id of the parent process."},
    {"uptime", "uptime(): number",
     "How many seconds the process has been running (monotonic)."},
    {"memoryUsage", "memoryUsage(): MemoryUsage",
     "Returns the resident-set size of the process. The `heapTotal`, "
     "`heapUsed`, `external` and `arrayBuffers` fields keep the Node result "
     "shape but are always `0` because cp3 has no managed heap."},
    {"hrtime", "hrtime(): [number, number]",
     "Returns `[seconds, nanoseconds]` since an unspecified monotonic origin. "
     "Subtract two results to measure an interval."},
    {"platform", "platform: string",
     "The OS platform: `\"win32\"`, `\"darwin\"` or `\"linux\"`."},
    {"arch", "arch: string",
     "The CPU architecture: `\"x64\"` or `\"arm64\"`."},
    {"version", "version: string", "The CP3 runtime version string."},
    {"exitCode", "exitCode: number",
     "The stored process exit code (default `0`). Assigning it does not exit."},
    {"nextTick", "nextTick(callback: () => void): void",
     "Queues `callback` on the next-tick queue: it runs before timers and "
     "promise continuations, in FIFO order."},
     {"stdout", "stdout: StdioStream",
      "A writable stream for the process standard output; `.write(s)` emits "
      "`s` with no newline, then flushes."},
     {"stderr", "stderr: StdioStream",
      "A writable stream for the process standard error; `.write(s)` emits "
      "`s` with no newline, then flushes."},
};

// The documented fields of an `ExecResult` value.
const DocRow kExecResultRows[] = {
    {"stdout", "stdout: string", "Everything the command wrote to stdout."},
    {"stderr", "stderr: string", "Everything the command wrote to stderr."},
    {"exitCode", "exitCode: number", "The exit code the command finished with."},
};

// The documented members of a `SpawnHandle` value.
const DocRow kSpawnHandleRows[] = {
    {"on", "on(event: \"data\" | \"stderr\" | \"exit\", listener: (value: string) => void): void",
     "Registers a listener for the `\"data\"` (stdout chunks), `\"stderr\"` "
     "(stderr chunks) or `\"exit\"` (the exit code) event."},
    {"write", "write(input: string): void",
     "Writes `input` to the child's stdin."},
    {"kill", "kill(): void",
     "Ends the child process."},
};

// The documented fields of a `memoryUsage()` value.
const DocRow kMemoryUsageRows[] = {
    {"rss", "rss: number", "The resident set size of the process in bytes."},
    {"heapTotal", "heapTotal: number", "Always `0`: cp3 has no managed heap."},
    {"heapUsed", "heapUsed: number", "Always `0`: cp3 has no managed heap."},
    {"external", "external: number", "Always `0`: not tracked by cp3."},
    {"arrayBuffers", "arrayBuffers: number", "Always `0`: not tracked by cp3."},
};

}  // namespace

Type Analyzer::processObjectType(const std::string& name) {
    std::vector<Field> fields;
    if (name == kExecResult) {
        fields.push_back(Field{"stdout", Type::string(), false});
        fields.push_back(Field{"stderr", Type::string(), false});
        fields.push_back(Field{
            "exitCode",
            stdIntegerType(StdIntegerKind::ExitCode, options_.typeSystem),
            false});
    } else if (name == kSpawnHandle) {
        // The three documented events ("data"/"stderr"/"exit") are call-site
        // arguments of `on`, so the field is the listener-taking method.
        fields.push_back(Field{"on",
                               Type::function({Type::string(), callbackOf(1)},
                                              Type::voidType()),
                               false});
        fields.push_back(
            Field{"write", Type::function({Type::string()}, Type::voidType()), false});
        fields.push_back(Field{"kill", Type::function({}, Type::voidType()), false});
    } else if (name == kMemoryUsage) {
        fields.push_back(Field{"rss", Type::number(), false});
        fields.push_back(Field{"heapTotal", Type::number(), false});
        fields.push_back(Field{"heapUsed", Type::number(), false});
        fields.push_back(Field{"external", Type::number(), false});
        fields.push_back(Field{"arrayBuffers", Type::number(), false});
    } else if (name == kStdioStream) {
        fields.push_back(Field{"write",
                               Type::function({Type::string()}, Type::voidType()),
                               false});
    } else {
        return Type::unknown();
    }
    Type object = makeObjectType(std::move(fields), false, false, {});
    // makeObjectType keys the shape by its structural label, while codegen
    // (src/std/process/codegen.cpp) recognises these markers by NAME and
    // memberOfProbe's shapeOf looks up by name, so the marker name needs the
    // same shape registered under it (the fields are identical).
    const auto label = shapes_.find(object.name);
    if (label != shapes_.end()) {
        Shape shape = label->second;
        object.name = name;
        shapes_[name] = std::move(shape);
    }
    return object;
}

std::optional<Type> Analyzer::processMember(const std::string& prop) {
    if (prop == "exit") {
        return Type::function(
            {stdIntegerType(StdIntegerKind::ExitCode, options_.typeSystem)},
            Type::voidType());
    }
    if (prop == "argv") return Type::array(Type::string());
    if (prop == "exec") {
        // Buffered: resolves once with the full result. A rejection is
        // reserved for a command that cannot be launched at all (docs §04);
        // the exit code is a field, never a rejection.
        return Type::function({Type::string(), Type::array(Type::string())},
                              promiseOf(processObjectType(kExecResult)));
    }
    if (prop == "spawn") {
        return Type::function({Type::string(), Type::array(Type::string())},
                              processObjectType(kSpawnHandle));
    }
    if (prop == "env") {
        // docs §04 pins `Map<string, string>`; the collections family (D44)
        // owns the type and its members, so this is just the nominal `Map`
        // instance (the swap point recorded before the merge, now closed).
        Type env(TypeKind::Class, "Map");
        env.parts.push_back(Type::string());
        env.parts.push_back(Type::string());
        return env;
    }
    if (prop == "cwd") return Type::function({}, Type::string());
    if (prop == "chdir")
        return Type::function({Type::string()}, Type::voidType());
    if (prop == "pid") {
        return stdIntegerType(StdIntegerKind::Pid, options_.typeSystem);
    }
    if (prop == "ppid") {
        return stdIntegerType(StdIntegerKind::Pid, options_.typeSystem);
    }
    if (prop == "uptime") return Type::function({}, Type::number());
    if (prop == "memoryUsage") {
        return Type::function({}, processObjectType(kMemoryUsage));
    }
    if (prop == "hrtime") {
        const Type t = makeObjectType({}, false, true,
                                      {Type::number(), Type::number()});
        return Type::function({}, t);
    }
    if (prop == "nextTick") {
        return Type::function(
            {Type::function({}, Type::voidType())}, Type::voidType());
    }
    if (prop == "platform" || prop == "arch" || prop == "version") {
        return Type::string();
    }
    if (prop == "exitCode") {
        return stdIntegerType(StdIntegerKind::ExitCode, options_.typeSystem);
    }
    if (prop == "stdout" || prop == "stderr") {
        return processObjectType(kStdioStream);
    }
    return std::nullopt;
}

std::vector<std::string> processMemberNames() {
    return {"exit", "argv", "exec", "spawn", "env",
            "cwd",  "chdir", "pid",  "ppid",  "uptime", "memoryUsage", "hrtime",
            "stdout", "stderr", "nextTick", "platform", "arch", "version",
            "exitCode"};
}

StdMemberDoc processMemberDoc(const std::string& prop) {
    return memberDoc(kProcessRows, prop);
}

StdMemberDoc processExecResultDoc(const std::string& prop) {
    return memberDoc(kExecResultRows, prop);
}

StdMemberDoc processSpawnHandleDoc(const std::string& prop) {
    return memberDoc(kSpawnHandleRows, prop);
}

StdMemberDoc processMemoryUsageDoc(const std::string& prop) {
    return memberDoc(kMemoryUsageRows, prop);
}

std::vector<std::string> processExecResultMemberNames() {
    return docRowNames(kExecResultRows);
}

std::vector<std::string> processSpawnHandleMemberNames() {
    return docRowNames(kSpawnHandleRows);
}

std::vector<std::string> processMemoryUsageMemberNames() {
    return docRowNames(kMemoryUsageRows);
}

}  // namespace cp3
