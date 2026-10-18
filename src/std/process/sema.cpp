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
// codegen (src/std/process/codegen.cpp) recognises by name; the shapes are
// OPEN, because only the documented fields are typed and the rest of the
// not-yet-typed surface stays permissive (the landed network convention).
//
// DEPENDENCY SEAM - `process.env`: the docs pin `Map<string, string>` from
// the (in-flight, not yet landed) collections family. Until it lands this
// file types a minimal fixed placeholder, `env.get(name): string | undefined`
// (the pinned `process_env_get_unset` behavior). TODO(collections): replace
// the `env` arm of `processMember` with the landed Map<string, string>; the
// exact swap point is marked there.
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace cp3 {

namespace {

const char* const kExecResult = "$process:ExecResult";
const char* const kSpawnHandle = "$process:SpawnHandle";

/// `(p0, p1, ...) => void` with permissive parameters: the documented event
/// listener shapes share one grammar and the parameters are typed
/// contextually by codegen (the runtime callable's declared types).
Type callbackOf(std::size_t arity) {
    return Type::function(std::vector<Type>(arity, ambient()), Type::voidType());
}

}  // namespace

Type Analyzer::processObjectType(const std::string& name) {
    std::vector<Field> fields;
    if (name == kExecResult) {
        fields.push_back(Field{"stdout", Type::string(), false});
        fields.push_back(Field{"stderr", Type::string(), false});
        fields.push_back(Field{"exitCode", Type::number(), false});
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
    } else {
        return Type::unknown();
    }
    Type object = makeObjectType(std::move(fields), true, false, {});
    object.name = name;
    return object;
}

std::optional<Type> Analyzer::processMember(const std::string& prop) {
    if (prop == "exit") {
        return Type::function({Type::number()}, Type::voidType());
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
        // ---------------------------------------------------------------
        // TODO(collections): THE SWAP POINT for `process.env`. Replace this
        // placeholder object with the landed collections `Map<string, string>`
        // (docs §04 pins `env: Map<string, string>`). `get(missing)` must keep
        // typing as `string | undefined` and keep lowering to
        // `::cp3::runtime::process::envGet`; once the real Map lands, sema's
        // member lookup comes from the collections family instead of this arm
        // and the placeholder below is deleted. Re-verify after the swap:
        // `process_env_get_unset` (the `=== undefined` pin).
        // ---------------------------------------------------------------
        std::vector<Type> value;
        value.push_back(Type::string());
        value.push_back(Type::undefined());
        Field get;
        get.name = "get";
        get.type = Type::function({Type::string()}, Type::unionOf(std::move(value)));
        std::vector<Field> fields;
        fields.push_back(std::move(get));
        return makeObjectType(std::move(fields), false, false, {});
    }
    return std::nullopt;
}

std::vector<std::string> processMemberNames() {
    return {"exit", "argv", "exec", "spawn", "env"};
}

}  // namespace cp3
