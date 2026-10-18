// =============================================================================
// src/std/console/codegen.cpp - `std/console` lowering.
//
// OWNERSHIP: the std/console family (src/std/console/**). The CallExpr
// dispatcher (emit/call.cpp) hands every `console.<member>(...)` call here; each
// member lowers onto its emitted-code contract (cp3/std/console.hpp):
//
//   log/info    -> cp3::runtime::println(joined)                (stdout)
//   warn/error  -> ::cp3::runtime::console::warn/error(...)     (stderr)
//   assert      -> ::cp3::runtime::console::assertCondition(cond, ...)
//   clear       -> ::cp3::runtime::console::clear()
//   input       -> ::cp3::runtime::console::input(prompt?)
//   readKey     -> ::cp3::runtime::console::readKey()
//   on/off      -> ::cp3::runtime::console::on/off(event, listener)
//   hide/show   -> ::cp3::runtime::console::hide()/show()
//
// Every argument is rendered through tsCall; an object literal is the one
// exception (D49: `console.log({ a: 1 })` -> `{ a: 1 }`), emitted as an
// explicit `formatObject({...})` form. warn/error keep their family header
// (consoleStderr_); the other members pull it via needsConsoleAbi_, and the
// async members also need the loop in `main` (needsEventLoop_).
// =============================================================================
#include "detail/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "detail/cpp_writer.h"
#include "detail/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

constexpr const char* kConsoleNs = "::cp3::runtime::console::";

// The documented event names of D75; nothing else can ever fire.
bool knownEvent(const std::string& event) {
    return event == "keypress" || event == "line" || event == "close";
}

// The literal text of an object-literal field key ("" when the field is out of
// this wave's scope: computed, a method, or a non-name key).
std::string fieldKey(const ObjectProperty& p) {
    if (p.computed || p.isMethod || !p.key) return std::string();
    if (p.key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*p.key).name;
    }
    if (p.key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*p.key).value;
    }
    return std::string();
}

// A listener runs on the loop even when nothing is awaited, so `main` must
// drive it; the arrow's parameter names are typed for the body emission only
// (sema keeps callback parameters permissive, like std/network's handlers).
std::string listenerParamType() { return "std::string"; }

}  // namespace

std::string Generator::emitConsoleCall(const CallExpr& c, const MemberExpr& m) {
    if (!consoleImported_) {
        notImplemented(c, "console used without `import console from \"std/console\"`");
        return "0.0";
    }
    const std::string& prop = m.property;
    if (prop == "log" || prop == "info") return emitConsoleLogCall(c);
    if (prop == "warn" || prop == "error") return emitConsoleStreamCall(c, prop);
    if (prop == "assert") return emitConsoleAssert(c);
    if (prop == "input") return emitConsoleInput(c);
    if (prop == "on" || prop == "off") return emitConsoleListener(c, prop);
    if (prop == "readKey") {
        needsConsoleAbi_ = true;
        needsEventLoop_ = true;
        return std::string(kConsoleNs) + "readKey()";
    }
    if (prop == "clear" || prop == "hide" || prop == "show") {
        needsConsoleAbi_ = true;
        return std::string(kConsoleNs) + prop + "()";
    }
    notImplemented(c, "console." + prop + "(...)");
    return "0.0";
}

// `.log` / `.info`: the joined string goes to `println` (stdout), exactly as
// before; only an object-literal argument changed form (formatObject).
std::string Generator::emitConsoleLogCall(const CallExpr& c) {
    if (c.arguments.empty()) {
        return "cp3::runtime::println(std::string())";
    }
    std::string joined = consoleArgText(*c.arguments.front());
    for (std::size_t i = 1; i < c.arguments.size(); ++i) {
        joined += " + std::string(\" \") + " + consoleArgText(*c.arguments[i]);
    }
    return "cp3::runtime::println(" + joined + ")";
}

// `.warn` / `.error`: the runtime renders the arguments itself and writes the
// line to stderr (consoleStderr_ also selects the family header).
std::string Generator::emitConsoleStreamCall(const CallExpr& c,
                                             const std::string& method) {
    consoleStderr_ = true;
    std::vector<std::string> args;
    for (const auto& arg : c.arguments) {
        if (arg) args.push_back(consoleArgText(*arg));
    }
    return std::string(kConsoleNs) + method + "(" + joinStrings(args, ", ") + ")";
}

// `.assert(condition, ...args)`: stderr only when the condition is falsy; the
// runtime never throws and never exits (STDLIB docs §04).
std::string Generator::emitConsoleAssert(const CallExpr& c) {
    needsConsoleAbi_ = true;
    if (c.arguments.empty() || !c.arguments.front()) {
        notImplemented(c, "console.assert(...)");
        return "0.0";
    }
    std::string out =
        std::string(kConsoleNs) + "assertCondition(" + cond(*c.arguments.front());
    for (std::size_t i = 1; i < c.arguments.size(); ++i) {
        if (!c.arguments[i]) continue;
        out += ", " + consoleArgText(*c.arguments[i]);
    }
    return out + ")";
}

// `.input(prompt?)` -> `console::input(prompt)`; the prompt is optional.
std::string Generator::emitConsoleInput(const CallExpr& c) {
    needsConsoleAbi_ = true;
    needsEventLoop_ = true;
    std::string prompt = "std::string()";
    if (!c.arguments.empty() && c.arguments.front() &&
        c.arguments.front()->kind != NodeKind::SpreadElement) {
        prompt = tsCall(*c.arguments.front());
    }
    return std::string(kConsoleNs) + "input(" + prompt + ")";
}

// `.on(event, listener)` / `.off(event, listener)`: the event must be one of
// the documented literals; the listener is an arrow (typed below) or a
// function value (a named function, so `off` can match its type - D75).
std::string Generator::emitConsoleListener(const CallExpr& c,
                                           const std::string& method) {
    needsConsoleAbi_ = true;
    needsEventLoop_ = true;
    if (c.arguments.size() < 2 || !c.arguments[0] || !c.arguments[1] ||
        c.arguments[0]->kind != NodeKind::StringLiteral) {
        notImplemented(c, "console." + method + "(event, listener)");
        return "0.0";
    }
    const std::string event =
        static_cast<const StringLiteral&>(*c.arguments[0]).value;
    if (!knownEvent(event)) {
        notImplemented(*c.arguments[0],
                       "console event name (\"keypress\"/\"line\"/\"close\")");
        return "0.0";
    }
    return std::string(kConsoleNs) + method + "(" + cppStringLiteral(event) +
           ", " + consoleListenerArg(*c.arguments[1]) + ")";
}

// A listener argument: an arrow becomes a lambda whose declared parameter type
// is the event's `string`; anything else is emitted as the function value it
// names (`handler`), which keeps `on`/`off` matching up by type.
std::string Generator::consoleListenerArg(const Expr& arg) {
    if (arg.kind != NodeKind::ArrowFunction) return expr(arg);
    const auto& arrow = static_cast<const ArrowFunction&>(arg);
    pushScope();
    for (const auto& paramPtr : arrow.params) {
        const Parameter* p = paramPtr.get();
        if (!p || !p->name || p->name->kind != NodeKind::Identifier) continue;
        declare(static_cast<const Identifier&>(*p->name).name, Type::string());
    }
    std::string text = emitArrow(arrow, {listenerParamType()});
    popScope();
    return text;
}

// One console argument: an object literal renders through the D49 object form;
// everything else goes through tsCall (a top-level string stays unquoted).
std::string Generator::consoleArgText(const Expr& arg) {
    if (arg.kind == NodeKind::SpreadElement) {
        notImplemented(arg, "spread argument");
        return "std::string()";
    }
    if (arg.kind == NodeKind::ObjectLiteral) {
        return consoleObjectText(static_cast<const ObjectLiteral&>(arg));
    }
    return tsCall(arg);
}

// `{ a: 1 }` -> `::cp3::runtime::formatObject({{"a", ...}})`: fields in
// property/insertion order, values pre-rendered (D49).
std::string Generator::consoleObjectText(const ObjectLiteral& literal) {
    needsConsoleAbi_ = true;
    std::string fields;
    for (const auto& propertyPtr : literal.properties) {
        const ObjectProperty* p = propertyPtr.get();
        if (!p || !p->value) continue;
        const std::string key = fieldKey(*p);
        if (key.empty()) {
            notImplemented(*p, "a computed or method object field");
            continue;
        }
        if (!fields.empty()) fields += ", ";
        fields += "{" + cppStringLiteral(key) + ", " +
                  consoleNestedValue(*p->value) + "}";
    }
    return "::cp3::runtime::formatObject({" + fields + "})";
}

// A value nested inside an object literal. `formatObject` takes pre-rendered
// values, so an object/array value is rendered by its own formatter while a
// primitive goes through `formatNested` (a string becomes quoted, JS style).
std::string Generator::consoleNestedValue(const Expr& value) {
    if (value.kind == NodeKind::ObjectLiteral) {
        return consoleObjectText(static_cast<const ObjectLiteral&>(value));
    }
    if (value.kind == NodeKind::ArrayLiteral) {
        return tsCall(value);  // already-rendered text: `[1, 2, 3]`
    }
    const CppInfo ci = info(value);
    if (ci.shape == Shape::Number) {
        // The double overload keeps a plain int spelling unambiguous.
        return "::cp3::runtime::formatNested(static_cast<double>(" +
               expr(value) + "))";
    }
    if (ci.shape == Shape::String || ci.shape == Shape::Bool ||
        ci.shape == Shape::Undefined || ci.shape == Shape::Null) {
        return "::cp3::runtime::formatNested(" + expr(value) + ")";
    }
    notImplemented(value, "this object field value");
    return "std::string()";
}

}  // namespace cp3
