// =============================================================================
// src/std/os/sema.cpp - `std/os` member typing and documentation.
//
// OWNERSHIP: the std/os family (src/std/os/**). The member table moved here
// from src/sema/src/checks/std_check.cpp (D72 stage 2); that core unit keeps
// the namespace dispatch (stdNamespaceMember / stdNamespaceMemberNames) and
// forwards here, so the rest of sema never needs to change for this family.
//
// The same table drives member typing, the CP3305 candidate list and the
// documentation the editor shows, so the three can never drift apart. os is a
// CLOSED family (std_check.cpp isClosedStdNamespace): a member outside the
// table is CP3305 with these names as candidates, never an untyped fallthrough.
//
// The signatures and the descriptions are the documented ones
// (docs/standard-library/04-imported-std-modules.md, os section): Node-style
// OS info, a thin wrapper over the OS calls.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

Type zeroToString() { return Type::function({}, Type::string()); }

const MemberRow kOsRows[] = {
    {"platform", zeroToString, "platform(): string",
     "Returns the platform name: `\"windows\"`, `\"macos\"` or `\"linux\"`."},
    {"homedir", zeroToString, "homedir(): string",
     "Returns the path of the current user's home directory."},
    {"tmpdir", zeroToString, "tmpdir(): string",
     "Returns the path of the system's temporary directory."},
    {"hostname", zeroToString, "hostname(): string",
     "Returns the host name of the machine."},
    {"cpus", zeroToNumber, "cpus(): number",
     "Returns the number of logical CPU cores."},
    {"totalMemory", zeroToNumber, "totalMemory(): number",
     "Returns the total amount of system memory in bytes."},
    {"freeMemory", zeroToNumber, "freeMemory(): number",
     "Returns the amount of free system memory in bytes."},
    {"totalmem", zeroToNumber, "totalmem(): number",
     "Returns the total amount of system memory in bytes (the Node "
     "`os.totalmem()` name)."},
    {"freemem", zeroToNumber, "freemem(): number",
     "Returns the amount of free system memory in bytes (the Node "
     "`os.freemem()` name)."},
    // `loadavg()` and `networkInterfaces()` return tuples/arrays and are
    // special-cased in `osMember` because the member table cannot build
    // tuple types; their documentation lives here anyway.
    {"loadavg", zeroToNumber, "loadavg(): [number, number, number]",
     "Returns the 1, 5 and 15 minute load averages as a `[number, number, "
     "number]` tuple. On platforms without load averages the values are "
     "zero."},
    {"networkInterfaces", zeroToNumber,
     "networkInterfaces(): Array<[string, string, string, boolean]>",
     "Returns one `[name, address, family, internal]` tuple per network "
     "interface. `family` is `\"IPv4\"` or `\"IPv6\"`; `internal` marks "
     "loopback-only interfaces."},
};

}  // namespace

std::optional<Type> Analyzer::osMember(const std::string& prop) {
    if (prop == "loadavg") {
        const Type t = makeObjectType({}, false, true,
                                      {Type::number(), Type::number(),
                                       Type::number()});
        return Type::function({}, t);
    }
    if (prop == "networkInterfaces") {
        const Type info =
            makeObjectType({}, false, true,
                           {Type::string(), Type::string(), Type::string(),
                            Type::boolean()});
        return Type::function({}, Type::array(info));
    }
    return lookupMember(kOsRows, prop);
}

std::vector<std::string> osMemberNames() { return memberNames(kOsRows); }

StdMemberDoc osMemberDoc(const std::string& prop) {
    return memberDoc(kOsRows, prop);
}

}  // namespace cp3
