// =============================================================================
// src/std/console/sema.cpp - `std/console` member typing and documentation.
//
// OWNERSHIP: the std/console family (src/std/console/**). The implemented-set
// table and the member function moved here from
// src/sema/src/checks/std_check.cpp (D72 stage 2); that core unit keeps the
// namespace dispatch (stdNamespaceMember / stdNamespaceMemberNames) and
// forwards here, so the rest of sema never needs to change for this family.
//
// Console is a CLOSED but surface-aware family (std_check.cpp
// isClosedStdNamespace): the implemented set below is what a typo is ranked
// against with "did you mean". Since the round-1 wave all twelve documented
// members are implemented, so every one of them is a suggestion candidate and
// an unknown member is exactly the documented-shape CP3305.
//
// The same table drives member typing, the CP3305 candidate list and the
// documentation the editor shows, so the three can never drift apart. The
// signatures and the descriptions are the documented ones
// (docs/standard-library/04-imported-std-modules.md, console section).
// =============================================================================
#include "detail/analyzer.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "detail/std_tables.hpp"

namespace cp3 {

namespace {

// The documented members (docs/standard-library/04-imported-std-modules.md
// §console): log, info, warn, error, input, readKey, clear, assert, on, off,
// hide, show - plus the v0.2 debug helpers dir, trace, count, countReset,
// group, groupEnd, table, time, timeEnd. All are real, so all are candidate
// suggestions for a typo.
//
// The members type as ambient (permissive signatures, like the other std
// families' surface members): the documented signature below is the precise
// spelling, and anything else keeps the CP3305 closed-shape report with the
// implemented set as candidates.
// The listener surface (D75) has a real signature: `on(event, listener)` and
// `off(event, listener)` take a string event and a listener. The signature is
// what gives the call its arity check (CP3403) and the event's type check
// (CP3401); the listener itself stays permissive, because a listener may
// declare the value parameter or none.
Type consoleListenerType() {
    return Type::function(
        {Type::string(), Type::function({Type::string()}, Type::voidType())},
        Type::voidType());
}

const MemberRow kConsoleRows[] = {
    {"log", ambient, "log(...args: Printable[]): void",
     "Prints the arguments to **stdout**, separated by a space and followed by "
     "a newline.\n\n"
     "**Parameters**\n\n"
     "- `args` - the values to print. A user class may implement "
     "`toString(): string` to control its own printed form; without one, a "
     "default representation is generated for the classes that are actually "
     "printed.\n\n"
     "**Example**\n\n"
     "```cp3\n"
     "console.log(\"listening on\", port);\n"
     "```\n\n"
     "Use `console.error` or `console.warn` when the message belongs on "
     "stderr rather than normal output."},
    {"info", ambient, "info(...args: Printable[]): void",
     "Prints the arguments to **stdout**, separated by a space and followed by "
     "a newline - the informational counterpart of `log`.\n\n"
     "**Example**\n\n```cp3\nconsole.info(\"cache refreshed\");\n```"},
    {"warn", ambient, "warn(...args: Printable[]): void",
     "Prints the arguments to **stderr**, separated by a space and followed by "
     "a newline.\n\n**Example**\n\n```cp3\nconsole.warn(\"using fallback configuration\");\n```"},
    {"error", ambient, "error(...args: Printable[]): void",
     "Prints the arguments to **stderr**, separated by a space and followed by "
     "a newline - the error counterpart of `warn`.\n\n"
     "**Example**\n\n```cp3\nconsole.error(\"request failed\", error);\n```"},
    {"dir", ambient, "dir(value: unknown): void",
     "Prints the value like `log` (objects in field order). Inspection depth "
     "options are ignored in v0.2.\n\n"
     "**Example**\n\n```cp3\nconsole.dir({ a: 1 });\n```"},
    {"trace", ambient, "trace(...args: Printable[]): void",
     "Prints a `Trace:` line with the arguments (no stack in v0.2).\n\n"
     "**Example**\n\n```cp3\nconsole.trace(\"checkpoint\", n);\n```"},
    {"count", ambient, "count(label?: string): void",
     "Prints `label: N` for a per-label counter (default label `default`); "
     "each call increments before printing."},
    {"countReset", ambient, "countReset(label?: string): void",
     "Resets a `count` label so the next `count` prints 1 again."},
    {"group", ambient, "group(...args: Printable[]): void",
     "Prints the arguments, then indents every following stdout line by two "
     "spaces until `groupEnd`."},
    {"groupEnd", ambient, "groupEnd(): void",
     "Closes the innermost open group (never below zero)."},
    {"table", ambient, "table(rows: unknown[]): void",
     "Prints an `(index) (value)` header then one `i value` line per array "
     "element, or one `key value` line per field of an object literal."},
    {"time", ambient, "time(label?: string): void",
     "Starts a per-label monotonic timer (default label `default`); "
     "`timeEnd` with the same label prints the elapsed milliseconds. Restarting "
     "an existing label discards its previous start."},
    {"timeEnd", ambient, "timeEnd(label?: string): void",
     "Prints `label: <ms>ms` for the timer `time` started under that label "
     "(default label `default`) and removes it. A label that was never "
     "started prints a warning to stderr instead."},
    {"time", ambient, "time(label?: string): void",
     "Starts a per-label monotonic timer (default label `default`). "
     "`console.timeEnd` with the same label prints the elapsed milliseconds."},
    {"timeEnd", ambient, "timeEnd(label?: string): void",
     "Prints `label: <ms>ms` for a `console.time` label and removes it. An "
     "unknown label prints `label: 0ms`."},
    {"input", ambient, "input(prompt?: string): Promise<string>",
     "Reads one line from stdin and resolves with it; on EOF it resolves with "
     "an empty string instead of rejecting.\n\n"
     "**Parameters**\n\n"
     "- `prompt` - the text written to the terminal before the line is read.\n\n"
     "The read is asynchronous: it never blocks the event loop, exactly like "
     "every other I/O primitive in the standard library."},
    {"readKey", ambient, "readKey(): Promise<string>",
     "Reads a single keypress and resolves with it - a named key such as "
     "`\"ArrowUp\"` or `\"Enter\"`, or the literal character typed. The "
     "terminal is put into raw mode while the read is pending."},
    {"clear", ambient, "clear(): void",
     "Clears the terminal screen."},
    {"assert", ambient, "assert(condition: boolean, ...args: Printable[]): void",
     "Logs the arguments to **stderr** only when `condition` is false. It "
     "never throws and never exits the program - it is a diagnostic aid, not "
     "a control-flow mechanism.\n\n"
     "**Parameters**\n\n"
     "- `condition` - the condition to check.\n"
     "- `args` - the values to log when the condition is false."},
    {"on", consoleListenerType,
     "on(event: \"keypress\" | \"line\" | \"close\", "
     "listener: (value: string) => void): void",
     "Keeps listening: `\"keypress\"` delivers every key, `\"line\"` delivers "
     "each completed line without its terminator, and `\"close\"` fires once "
     "with `\"\"` when stdin reaches EOF. Listeners persist until `off` "
     "removes them."},
    {"off", consoleListenerType,
     "off(event: \"keypress\" | \"line\" | \"close\", "
     "listener: (value: string) => void): void",
     "Stops listening: the listener is removed from the event it was "
     "registered for."},
    {"hide", ambient, "hide(): void",
     "Hides the console window the program runs in (Windows). The program "
     "keeps running; on platforms without a console window of its own this "
     "is a no-op."},
    {"show", ambient, "show(): void",
     "Restores a console window hidden by `hide()`. When there is nothing to "
     "restore, this is a no-op."},
};

}  // namespace

std::optional<Type> Analyzer::consoleMember(const std::string& prop) {
    return lookupMember(kConsoleRows, prop);
}

std::vector<std::string> consoleMemberNames() { return memberNames(kConsoleRows); }

std::vector<std::string> consoleMemberArgumentValues(
    const std::string& prop, std::size_t argumentIndex) {
    // D75's event list is a finite API contract, unlike EventEmitter's
    // application-defined event names. Keep completion and codegen checks
    // tied to this one list.
    if (argumentIndex == 0 && (prop == "on" || prop == "off")) {
        return {"keypress", "line", "close"};
    }
    return {};
}

StdMemberDoc consoleMemberDoc(const std::string& prop) {
    return memberDoc(kConsoleRows, prop);
}

}  // namespace cp3
