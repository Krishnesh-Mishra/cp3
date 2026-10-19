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
        // docs §04 pins `Map<string, string>`; the collections family (D44)
        // owns the type and its members, so this is just the nominal `Map`
        // instance (the swap point recorded before the merge, now closed).
        Type env(TypeKind::Class, "Map");
        env.parts.push_back(Type::string());
        env.parts.push_back(Type::string());
        return env;
    }
    return std::nullopt;
}

std::vector<std::string> processMemberNames() {
    return {"exit", "argv", "exec", "spawn", "env"};
}

}  // namespace cp3
