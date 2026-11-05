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
//   time/timeEnd-> ::cp3::runtime::console::time/timeEnd(label)
//
// Every argument is rendered through tsCall; an object literal is the one
// exception (D49: `console.log({ a: 1 })` -> `{ a: 1 }`), emitted as an
// explicit `formatObject({...})` form. warn/error keep their family header
// (consoleStderr_); the other members pull it via needsConsoleAbi_, and the
// async members also need the loop in `main` (needsEventLoop_).
//
// A spread argument (`console.log(...parts)`) contributes the array's
// elements to the line, so the call lowers onto the runtime's `*Parts` form
// (cp3/std/console.hpp): the parts are rendered and joined with one space,
// exactly like the variadic form.
// =============================================================================
#include "detail/generator/generator.h"

#include <cstddef>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"
#include "cp3/std_surface.hpp"
#include "detail/class/class_model.h"
#include "detail/generator/cpp_writer.h"
#include "detail/generator/type_map.h"

namespace cp3 {

using namespace detail;

namespace {

constexpr const char* kConsoleNs = "::cp3::runtime::console::";

// The literal text of an object-literal field key ("" when the field is out of
// this wave's scope: a method, or a non-name key).
std::string fieldKey(const ObjectProperty& p) {
    if (p.isMethod || !p.key) return std::string();
    if (p.key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*p.key).name;
    }
    if (p.key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*p.key).value;
    }
    if (p.key->kind == NodeKind::NumberLiteral) {
        return static_cast<const NumberLiteral&>(*p.key).raw;
    }
    return std::string();
}

// A listener runs on the loop even when nothing is awaited, so `main` must
// drive it; the arrow's parameter names are typed for the body emission only
// (sema keeps callback parameters permissive, like std/network's handlers).
std::string listenerParamType() { return "std::string"; }

// Does the call carry a spread argument? A spread contributes several
// arguments to the line, so the call lowers onto the parts form.
bool hasSpread(const CallExpr& c) {
    for (const auto& arg : c.arguments) {
        if (arg && arg->kind == NodeKind::SpreadElement) return true;
    }
    return false;
}

}  // namespace

std::string Generator::emitConsoleCall(const CallExpr& c, const MemberExpr& m) {
    // The `console` name reaches this lowering only when it is the imported
    // standard module: the dispatcher (emit/call.cpp) checks the import and
    // lets a user binding named `console` keep its own calls.
    const std::string& prop = m.property;
    if (prop == "log" || prop == "info") return emitConsoleLogCall(c);
    if (prop == "warn" || prop == "error") return emitConsoleStreamCall(c, prop);
    if (prop == "dir" || prop == "trace" || prop == "group") {
        needsConsoleAbi_ = true;
        const std::string parts = consolePartsText(c, 0);
        const std::string fn = prop == "dir" ? "dirParts"
                               : prop == "trace" ? "traceParts"
                                                 : "groupStart";
        return std::string(kConsoleNs) + fn + "(" + parts + ")";
    }
    if (prop == "groupEnd") {
        needsConsoleAbi_ = true;
        return std::string(kConsoleNs) + "groupEnd()";
    }
    if (prop == "count" || prop == "countReset" || prop == "time" ||
        prop == "timeEnd") {
        needsConsoleAbi_ = true;
        std::string label = "\"default\"";
        if (!c.arguments.empty() && c.arguments.front() &&
            c.arguments.front()->kind != NodeKind::SpreadElement) {
            label = tsCall(*c.arguments.front());
        }
        return std::string(kConsoleNs) + prop + "(" + label + ")";
    }
    if (prop == "time" || prop == "timeEnd") {
        needsConsoleAbi_ = true;
        std::string label = "\"default\"";
        if (!c.arguments.empty() && c.arguments.front() &&
            c.arguments.front()->kind != NodeKind::SpreadElement) {
            label = tsCall(*c.arguments.front());
        }
        return std::string(kConsoleNs) + prop + "(" + label + ")";
    }
    if (prop == "table") return emitConsoleTable(c);
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
    // A member outside the console surface is not a console call: the caller
    // (emit/call_args.cpp) hands it to the ordinary member call, which owns
    // a user value that shadows the console name. Sema rejects an unknown
    // member of the console namespace itself (CP3305, with the documented
    // names as candidates), so the console namespace never reaches here.
    return std::string();
}

// `.log` / `.info`: the parts go to `console::logParts` (stdout), so open
// groups indent the line. A spread argument contributes the array's elements
// to the line, exactly like the variadic form.
std::string Generator::emitConsoleLogCall(const CallExpr& c) {
    const std::string parts = consolePartsText(c, 0);
    if (!parts.empty()) {
        // The parts form lives in the console family's header.
        needsConsoleAbi_ = true;
        return std::string(kConsoleNs) + "logParts(" + parts + ")";
    }
    // The spread was reported; the plain form keeps the unit compilable.
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
    if (hasSpread(c)) {
        const std::string parts = consolePartsText(c, 0);
        if (!parts.empty()) {
            // The parts form lives in the console family's header.
            needsConsoleAbi_ = true;
            return std::string(kConsoleNs) + method + "Parts(" + parts + ")";
        }
        // The spread was reported; the plain form keeps the unit compilable.
    }
    std::vector<std::string> args;
    for (const auto& arg : c.arguments) {
        if (arg) args.push_back(consoleArgText(*arg));
    }
    return std::string(kConsoleNs) + method + "(" + joinStrings(args, ", ") + ")";
}

// `.assert(condition, ...args)`: stderr only when the condition is falsy; the
// runtime never throws and never exits (STDLIB docs §04). An omitted
// condition is falsy (Node parity: `console.assert()` reports the failure).
std::string Generator::emitConsoleAssert(const CallExpr& c) {
    needsConsoleAbi_ = true;
    if (c.arguments.empty() || !c.arguments.front()) {
        return std::string(kConsoleNs) + "assertCondition(false)";
    }
    if (hasSpread(c)) {
        const std::string parts = consolePartsText(c, 1);
        if (!parts.empty()) {
            // The parts form lives in the console family's header.
            needsConsoleAbi_ = true;
            return std::string(kConsoleNs) + "assertConditionParts(" +
                   cond(*c.arguments.front()) + ", " + parts + ")";
        }
        // The spread was reported; the plain form keeps the unit compilable.
    }
    std::string out =
        std::string(kConsoleNs) + "assertCondition(" + cond(*c.arguments.front());
    for (std::size_t i = 1; i < c.arguments.size(); ++i) {
        if (!c.arguments[i]) continue;
        out += ", " + consoleArgText(*c.arguments[i]);
    }
    return out + ")";
}

// `.table(x)`: a compact two-column listing. An array prints an
// `(index) (value)` header then one `i value` line per element; an object
// literal prints one `key value` line per field, in insertion order. Only
// formattable elements are supported (the same values log can print).
std::string Generator::emitConsoleTable(const CallExpr& c) {
    needsConsoleAbi_ = true;
    if (c.arguments.empty() || !c.arguments.front()) {
        return "0.0";
    }
    const Expr& arg = *c.arguments.front();
    if (arg.kind == NodeKind::ObjectLiteral) {
        const auto& literal = static_cast<const ObjectLiteral&>(arg);
        std::string body;
        for (const auto& propertyPtr : literal.properties) {
            const ObjectProperty* p = propertyPtr.get();
            if (!p || !p->value) continue;
            const std::string key = fieldKey(*p);
            if (key.empty()) continue;
            body += " ::cp3::runtime::console::writeLineStdout(std::string(\"" +
                    key + "\") + \" \" + " +
                    consoleNestedValue(*p->value) + ");";
        }
        return "([&]{ " + body + " }())";
    }
    const Type type = effectiveType(arg);
    const Type elem =
        type.kind == TypeKind::Array && !type.parts.empty()
            ? type.parts.front()
            : Type::unknown();
    const CppInfo ec = classify(elem);
    if (!ec.representable || !isPrimitiveShape(ec.shape) ||
        !valueFormattable(elem)) {
        sink_.error(diagcode::kInvalidArgumentValue, arg.span,
                    "console.table expects an array of printable values or an "
                    "object literal");
        return "0.0";
    }
    return "([&]{ const auto& cp3_t = (" + expr(arg) + "); " +
           "::cp3::runtime::console::writeLineStdout(\"(index) (value)\"); "
           "std::size_t cp3_i = 0; for (const auto& cp3_e : *cp3_t) { " +
           "::cp3::runtime::console::writeLineStdout(std::to_string(cp3_i++) + "
           "\" \" + ::cp3::runtime::formatValue(cp3_e)); } }())";
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
    if (c.arguments.size() < 2 || !c.arguments[0] || !c.arguments[1]) {
        // The arity is checked by sema (CP3403) and the event's type by the
        // listener signature (CP3401); a malformed argument list is a parser
        // recovery node. Keep the emitted C++ valid either way.
        return std::string(kConsoleNs) + method + "(\"\", [](std::string) {})";
    }
    if (c.arguments[0]->kind != NodeKind::StringLiteral) {
        sink_.error(diagcode::kInvalidArgumentValue, c.arguments[0]->span,
                    "console event must be a string literal: expected "
                    "\"keypress\", \"line\" or \"close\"");
        return "0.0";
    }
    const std::string event =
        static_cast<const StringLiteral&>(*c.arguments[0]).value;
    const std::vector<std::string> validEvents =
        stdNamespaceMemberArgumentValues("console", method, 0);
    bool known = false;
    for (const std::string& valid : validEvents) {
        if (event == valid) {
            known = true;
            break;
        }
    }
    if (!known) {
        // D75: the three documented events are the only ones that can fire,
        // so any other name is a program error, not a missing feature.
        sink_.error(diagcode::kInvalidArgumentValue, c.arguments[0]->span,
                    "unknown console event '" + event +
                        "'; expected \"keypress\", \"line\" or \"close\"");
        return "0.0";
    }
    return std::string(kConsoleNs) + method + "(" + cppStringLiteral(event) + ", " +
           consoleListenerArg(*c.arguments[1]) + ")";
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

// One console argument: an object literal renders through the D49 object
// form; everything else goes through tsCall (a top-level string stays
// unquoted). A spread is handled by the parts form (consolePartsText), so it
// never reaches this helper.
std::string Generator::consoleArgText(const Expr& arg) {
    if (arg.kind == NodeKind::SpreadElement) {
        return "std::string()";
    }
    if (arg.kind == NodeKind::ObjectLiteral) {
        return consoleObjectText(static_cast<const ObjectLiteral&>(arg));
    }
    return tsCall(arg);
}

// The console argument list: the fixed arguments are rendered strings, a
// spread contributes the array's elements. The parts are collected in a
// vector in argument order (JS parity: the arguments are evaluated left to
// right) and the runtime joins them with one space. A spread needs the loop
// form; everything else is a braced initializer list so `await` inside an
// argument does not turn the synthetic lambda into a coroutine. "" when a
// spread has no C++ spelling (the report is raised).
std::string Generator::consolePartsText(const CallExpr& c, std::size_t from) {
    // The fixed-argument case constructs the vector directly. A braced
    // initializer list evaluates in order, and - unlike a lambda - it is not
    // a function, so an argument that contains `co_await` stays in the
    // enclosing coroutine.
    if (!hasSpread(c)) {
        std::string list;
        for (std::size_t i = from; i < c.arguments.size(); ++i) {
            const Expr* arg = c.arguments[i].get();
            if (!arg) continue;
            if (!list.empty()) list += ", ";
            list += consoleArgText(*arg);
        }
        return "std::vector<std::string>{" + list + "}";
    }
    std::string body;
    for (std::size_t i = from; i < c.arguments.size(); ++i) {
        const Expr* arg = c.arguments[i].get();
        if (!arg) continue;
        if (arg->kind == NodeKind::SpreadElement) {
            // The spread's array: its elements are the line's parts.
            const auto& spread = static_cast<const SpreadElement&>(*arg);
            const Type type = spread.argument
                                   ? effectiveType(*spread.argument)
                                   : Type::unknown();
            const CppInfo ec = type.kind == TypeKind::Array && !type.parts.empty()
                                   ? classify(type.parts.front())
                                   : CppInfo{};
            if (!ec.representable || !isPrimitiveShape(ec.shape)) {
                sink_.error(diagcode::kInvalidArgumentValue, arg->span,
                            "a spread argument must be an array of primitive "
                            "values");
                return std::string();
            }
            body += " for (const auto& cp3_e : *" + expr(*spread.argument) +
                    ") cp3_parts.push_back(::cp3::runtime::formatValue(cp3_e));";
            continue;
        }
        body += " cp3_parts.push_back(" + consoleArgText(*arg) + ");";
    }
    // The explicit return type matters: an argument may contain `await`, and
    // C++ forbids `co_await` inside a lambda whose return type is deduced.
    return "([&]() -> std::vector<std::string> { std::vector<std::string> "
           "cp3_parts;" +
           body + " return cp3_parts; }())";
}

// `{ a: 1 }` -> `::cp3::runtime::formatObject({{"a", ...}})`: fields in
// property/insertion order, values pre-rendered (D49).
std::string Generator::consoleObjectText(const ObjectLiteral& literal) {
    needsConsoleAbi_ = true;
    std::string fields;
    for (const auto& propertyPtr : literal.properties) {
        const ObjectProperty* p = propertyPtr.get();
        if (!p || !p->value) continue;
        std::string key;
        if (p->computed) {
            // A computed key names the property at runtime, so the key text is
            // the key webserverion's own value.
            if (!p->key) continue;
            const CppInfo ci = info(*p->key);
            if (ci.shape == Shape::String) {
                key = expr(*p->key);
            } else if (ci.shape == Shape::Number) {
                key = "::cp3::runtime::toString(" + expr(*p->key) + ")";
            } else if (ci.representable && valueFormattable(ci.type)) {
                key = "cp3_ts(" + expr(*p->key) + ")";
            } else {
                // The key's value is not known here, so the property has no
                // name to render under.
                sink_.error(diagcode::kInvalidArgumentValue, p->key->span,
                            "a computed property key must have a known value");
                continue;
            }
        } else {
            // A plain key: its text names the property.
            const std::string name = fieldKey(*p);
            if (name.empty()) continue;  // a method field (not parseable)
            key = cppStringLiteral(name);
        }
        if (!fields.empty()) fields += ", ";
        fields += "{" + key + ", " + consoleNestedValue(*p->value) + "}";
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
    // A class instance, an object, an array or an enum: the unit's own
    // formatter renders it, exactly as the top-level `console.log` does (a
    // value with no formatter is reported there).
    return tsCall(value);
}

}  // namespace cp3
