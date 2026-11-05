// =============================================================================
// cp3/std_surface.hpp - the std-namespace surface sema already implements.
//
// OWNERSHIP: sema module (src/sema/**). Declaration-only public header for
// callers outside sema (the LSP intelligence engine, src/cli). The definitions
// live where the tables do: src/sema/src/checks/std_check.cpp dispatches to the
// per-family units under src/std/<family>/sema.cpp, none of which are part of
// the public boundary.
//
// This is the exact surface the checker itself uses when it validates a
// member access on a std namespace (CP3305 candidates), so an editor that
// offers these names can never offer something the compiler rejects as
// unknown, and never hides something it resolves.
//
// The same tables carry the DOCUMENTATION of every member: the documented
// signature and the Markdown description the editor shows on hover and in the
// completion popup. A member with no documented entry answers an empty doc,
// which the caller renders as a visible gap rather than as nothing at all.
// =============================================================================
#pragma once

#include <string>
#include <cstddef>
#include <vector>

#include "cp3/host/options.hpp"

namespace cp3 {

// The documentation of one std member.
struct StdMemberDoc {
    std::string signature;  // the documented signature, e.g. "log(...args: Printable[]): void"
    std::string body;       // the Markdown description (summary, parameters, notes)

    bool empty() const noexcept {
        return signature.empty() && body.empty();
    }
};

// True for the std namespaces with a closed member table (Math / path / os /
// crypto / console / JSON / Date / fs / process / env): an unknown member is
// CP3305. The `network` namespace remains open for its top-level namespace
// operations, while its typed values have a closed surface below.
bool isClosedStdNamespace(const std::string& ns);

// The implemented member names of the closed namespace `ns`, in table order;
// empty when `ns` names no closed family.
std::vector<std::string> stdNamespaceMemberNames(const std::string& ns);

// The documentation of `ns`.`member` on a namespace surface (Math, path, os,
// console, JSON, Date, fs, process, env, crypto). Empty when the namespace or
// the member has no documented entry.
StdMemberDoc stdNamespaceMemberDoc(const std::string& ns,
                                   const std::string& member);

// Finite literal values accepted by a documented member argument. An empty
// result means the argument accepts an open-ended value set (or the member / 
// argument has no such metadata). Completion and codegen validation consume
// this same table so the editor cannot recommend a literal the compiler rejects.
std::vector<std::string> stdNamespaceMemberArgumentValues(
    const std::string& ns, const std::string& member,
    std::size_t argumentIndex);

// The documentation of a member of a std VALUE surface - a Date instance, a
// Map, a Set. Empty when the family or the member has no documented entry.
StdMemberDoc stdInstanceMemberDoc(const std::string& family,
                                   const std::string& member);

// The member names of the std VALUE surfaces (a Date instance, a Map, a Set).
// A member that is listed here but has no documentation is a visible gap, not
// a member that does not exist.
std::vector<std::string> stdInstanceMemberNames(const std::string& family);

// The documentation of the array method surface (`length`, `push`, `map`, ...).
StdMemberDoc stdArrayMemberDoc(const std::string& member);

// The member names of the array method surface.
std::vector<std::string> stdArrayMemberNames();

// The documentation of the string method surface (`length`, `charAt`, ...).
StdMemberDoc stdStringMemberDoc(const std::string& member);

// The member names of the string method surface.
std::vector<std::string> stdStringMemberNames();

// The member names exposed by a typed std/network value (HTTP server, request,
// response, fetch response, or WebSocket handle). The compiler and editor use
// this same table so completion cannot drift from accepted member access.
std::vector<std::string> networkObjectMemberNames(
    const std::string& objectTypeName);
StdMemberDoc networkObjectMemberDoc(
    const std::string& objectTypeName, const std::string& member,
    TypeSystem ts = TypeSystem::TypeScript);

}  // namespace cp3
