// =============================================================================
// src/sema/src/checks/std_check.cpp - the std-namespace dispatch + `String(x)`.
//
// Routes the typed views of the ambient std surfaces that `import ... from
// "std/..."` names, per STDLIB docs §04: `Math`, `path`, `os`, `process`,
// `crypto` and `console` / `JSON`, whose calls partly stay routed through
// `tryInferJsonCall` for their generics. Typing these names is what turns
// `let r = Math.random()` into a real declaration and enforces the builtin
// signatures in `check` (M2 open bug 9: `Math.abs("x")`, `Math.round()`).
//
// The member tables themselves live with their families
// (`src/std/<family>/sema.cpp`, D72 stage 2); this unit keeps the per-namespace
// switch and the shared suggestion machinery, so a new member lands in the
// family unit without touching core.
//
// Two shapes of namespace:
//   * CLOSED families (Math / path / os / crypto / console / JSON / Date /
//     fs / process / env): every documented member is listed in the family's
//     table, and an unknown member is CP3305 with "did you mean" candidates
//     from the implemented names (member_check.cpp consults
//     isClosedStdNamespace). Console is closed too: since the round-1 wave all
//     twelve documented members (assert/clear/input/readKey included) are
//     implemented, so the suggestion candidates cover the full documented set.
//   * OPEN families (network in src/std/network/sema.cpp, and any family with
//     no table at all): unknown members keep the old permissive ambient
//     behavior, so the pinned signature cases pass unchanged.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

namespace cp3 {

// -----------------------------------------------------------------------------
// Namespace dispatch
// -----------------------------------------------------------------------------

std::optional<Type> Analyzer::stdNamespaceMember(const std::string& ns,
                                                 const std::string& prop) {
    if (ns == "Math") return mathMember(prop);
    if (ns == "path") return pathMember(prop);
    if (ns == "os") return osMember(prop);
    if (ns == "process") return processMember(prop);
    if (ns == "env") return envMember(prop);
    if (ns == "crypto") return cryptoMember(prop);
    if (ns == "network") return networkMember(prop);
    if (ns == "fs") return fsMember(prop);
    if (ns == "io") return ioMember(prop);
    if (ns == "console") return consoleMember(prop);
    if (ns == "JSON") return jsonMember(prop);
    if (ns == "Date") return dateMember(prop);
    return std::nullopt;
}

// -----------------------------------------------------------------------------
// Surface helpers (declared in detail/analyzer.h)
// -----------------------------------------------------------------------------

bool isClosedStdNamespace(const std::string& ns) {
    return ns == "Math" || ns == "path" || ns == "os" || ns == "crypto" ||
           ns == "console" || ns == "JSON" || ns == "Date" || ns == "fs" ||
           ns == "io" || ns == "process" || ns == "env";
}

// The CP3305 candidate lists come from the family units, which own the tables.
std::vector<std::string> stdNamespaceMemberNames(const std::string& ns) {
    if (ns == "Math") return mathMemberNames();
    if (ns == "path") return pathMemberNames();
    if (ns == "os") return osMemberNames();
    if (ns == "process") return processMemberNames();
    if (ns == "env") return envMemberNames();
    if (ns == "JSON") return jsonMemberNames();
    if (ns == "crypto") return cryptoMemberNames();
    if (ns == "console") return consoleMemberNames();
    if (ns == "Date") return dateMemberNames();
    if (ns == "fs") return fsMemberNames();
    if (ns == "io") return ioMemberNames();
    return {};
}

// -----------------------------------------------------------------------------
// Documentation
// -----------------------------------------------------------------------------
//
// The same dispatch as above, for the documentation the editor shows: the
// documented signature and the Markdown description of one member. The tables
// live with their families, so the documentation can never describe a member
// the compiler does not have (or the other way round).

// The receiver markers of the families whose values carry documented fields
// (the same names the family units and the codegen recognise).
const char* const kFileStatsMarker = "$fs:FileStats";
const char* const kExecResultMarker = "$process:ExecResult";
const char* const kSpawnHandleMarker = "$process:SpawnHandle";

StdMemberDoc stdNamespaceMemberDoc(const std::string& ns,
                                   const std::string& member) {
    if (ns == "Math") return mathMemberDoc(member);
    if (ns == "Number") return numberStaticMemberDoc(member);
    if (ns == "path") return pathMemberDoc(member);
    if (ns == "os") return osMemberDoc(member);
    if (ns == "process") return processMemberDoc(member);
    if (ns == "env") return envMemberDoc(member);
    if (ns == "crypto") return cryptoMemberDoc(member);
    if (ns == "console") return consoleMemberDoc(member);
    if (ns == "JSON") return jsonMemberDoc(member);
    if (ns == "Date") return dateMemberDoc(member);
    if (ns == "fs") return fsMemberDoc(member);
    if (ns == "io") return ioMemberDoc(member);
    return StdMemberDoc{};
}

std::vector<std::string> stdNamespaceMemberArgumentValues(
    const std::string& ns, const std::string& member,
    std::size_t argumentIndex) {
    if (ns == "console") {
        return consoleMemberArgumentValues(member, argumentIndex);
    }
    return {};
}

StdMemberDoc stdInstanceMemberDoc(const std::string& family,
                                   const std::string& member) {
    if (family == "Date") return dateInstanceMemberDoc(member);
    if (family == "Number") return numberMemberDoc(member);
    if (family == "Map") return collectionsMemberDoc("Map", member);
    if (family == "Set") return collectionsMemberDoc("Set", member);
    if (family == kFileStatsMarker) return fsStatsMemberDoc(member);
    if (family == kExecResultMarker) return processExecResultDoc(member);
    if (family == kSpawnHandleMarker) return processSpawnHandleDoc(member);
    return StdMemberDoc{};
}

std::vector<std::string> stdInstanceMemberNames(const std::string& family) {
    if (family == "Date") return dateInstanceMemberNames();
    if (family == "Number") return numberMemberNames();
    if (family == "Map") return collectionsMemberNames("Map");
    if (family == "Set") return collectionsMemberNames("Set");
    if (family == kFileStatsMarker) return fsStatsMemberNames();
    if (family == kExecResultMarker) return processExecResultMemberNames();
    if (family == kSpawnHandleMarker) return processSpawnHandleMemberNames();
    return {};
}

StdMemberDoc stdArrayMemberDoc(const std::string& member) {
    return arrayMemberDoc(member);
}

std::vector<std::string> stdArrayMemberNames() { return arrayMemberNames(); }

StdMemberDoc stdStringMemberDoc(const std::string& member) {
    return stringMemberDoc(member);
}

std::vector<std::string> stdStringMemberNames() { return stringMemberNames(); }

std::string stdImportHint(const std::string& name) {
    struct ModuleHint {
        const char* binding;
        const char* specifier;
    };
    // The default-exported std modules (std_registry.cpp) whose import line a
    // bare name follows from. Named-only modules (std/env, std/collections,
    // ...) are handled by import validation instead.
    static const ModuleHint kHints[] = {
        {"Math", "std/math"},       {"JSON", "std/json"},
        {"console", "std/console"}, {"path", "std/path"},
        {"os", "std/os"},           {"crypto", "std/crypto"},
        {"fs", "std/fs"},           {"io", "std/io"},
        {"Date", "std/date"},       {"process", "std/process"},
    };
    for (const ModuleHint& hint : kHints) {
        if (name == hint.binding) {
            return "import " + std::string(hint.binding) + " from \"" +
                   hint.specifier + "\";";
        }
    }

    // Named-export modules. The command-line compiler must give the same
    // repair the LSP's auto-import offers; a bare `fetch(...)` without the
    // import is a normal "cannot find name" error, not a mysterious missing
    // builtin.
    struct NamedHint {
        const char* binding;
        const char* specifier;
    };
    static const NamedHint kNamedHints[] = {
        {"fetch", "std/network"},
        {"createServer", "std/network"},
        {"WebSocket", "std/network"},
        {"WebSocketServer", "std/network"},
        {"Buffer", "std/buffer"},
        {"env", "std/env"},
        {"Map", "std/collections"},
        {"Set", "std/collections"},
        {"Mutex", "std/collections"},
        {"Queue", "std/collections"},
        {"Stack", "std/collections"},
        {"PriorityQueue", "std/collections"},
        {"LRUCache", "std/collections"},
        {"RingBuffer", "std/collections"},
        {"BitSet", "std/collections"},
        {"MultiMap", "std/collections"},
    };
    for (const NamedHint& hint : kNamedHints) {
        if (name == hint.binding) {
            return "import { " + std::string(hint.binding) + " } from \"" +
                   hint.specifier + "\";";
        }
    }
    return {};
}

// -----------------------------------------------------------------------------
// `String(x)` global
// -----------------------------------------------------------------------------

// `String(value)`: the type is always `string` (the runtime lowers it onto
// toString). The argument is not constrained: JS `String` accepts anything,
// including arrays (which M1 cannot represent yet).
bool Analyzer::tryInferStringBuiltin(CallExpr& call, const Identifier& id,
                                     Type& out) {
    if (id.name != "String") return false;
    inferArgs(call);
    out = Type::string();
    return true;
}

}  // namespace cp3
