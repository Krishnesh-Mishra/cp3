// =============================================================================
// src/std/network/express/settings/codegen.cpp - Express settings + view
// engine call/read/write lowering.
//
// OWNERSHIP: the std/network/express/settings family
// (src/std/network/express/settings/**). Lowers the documented receivers
// onto cp3::runtime::express (cp3/std/network/express/settings.hpp):
//
//   app.enable/disable/enabled/set/get/engine/render
//   app.locals.<k> / app.locals["k"]   (read + write)
//   res.locals.<k> / res.locals["k"]   (read + write)
//   res.render(view, options?)          req.protocol
//
// The hooks are called from the shared dispatch units (WIRING.md): the
// member-call hook runs BEFORE the network hook (so `res.render` is not
// emitted as a network member call), the member-read hook runs before the
// network read hook, and the two assignment hooks run before the model /
// family write paths. The four helpers below (`expressSettingValueText`,
// `expressRenderOptionsText`, `expressSettingsArg`, `expressSettingsArgs`)
// are declared next to the hook declarations in generator.h (WIRING.md
// step 5).
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

#include "detail/generator/cpp_writer.h"
#include "detail/generator/mangle.h"
#include "detail/generator/type_map.h"
#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

using namespace detail;

namespace {

constexpr const char* kExpressApp = "$express:ExpressApp";
constexpr const char* kExpressLocals = "$express:Locals";
constexpr const char* kHttpRequest = "$network:HttpRequest";
constexpr const char* kHttpResponse = "$network:HttpResponse";
constexpr const char* kExpressNs = "::cp3::runtime::express::";

}  // namespace

// The C++ spelling of one `app.set` / locals value: the `SettingValue`
// carrier (string | number | boolean | null | string[]). A string-array
// literal becomes a `std::vector<std::string>`; anything else is a type
// error at the use site, not a silent stringification.
std::string Generator::expressSettingValueText(const Expr& value) {
    if (value.kind == NodeKind::UndefinedLiteral) {
        return std::string(kExpressNs) + "SettingValue()";
    }
    if (value.kind == NodeKind::ArrayLiteral) {
        const auto& literal = static_cast<const ArrayLiteral&>(value);
        std::vector<std::string> items;
        for (const auto& elementPtr : literal.elements) {
            const Expr* element = elementPtr.get();
            if (element == nullptr ||
                element->kind == NodeKind::SpreadElement) {
                reportTypeError(
                    value,
                    "type mismatch: a setting string list cannot contain a "
                    "spread element");
                return "0.0";
            }
            items.push_back(expr(*element));
        }
        return std::string(kExpressNs) +
               "SettingValue(std::vector<std::string>{" +
               joinStrings(items, ", ") + "})";
    }
    return std::string(kExpressNs) + "SettingValue(" + expr(value) + ")";
}

// The `Locals` value of a `res.render` / `app.render` options argument.
// An object literal becomes a bag built entry by entry (later keys win,
// like a JS object); a value of a plain object shape (a `const opts = {...}`
// binding) contributes its fields; a `Locals` value passes through;
// anything else is a type error. "" when there is no options argument.
std::string Generator::expressRenderOptionsText(const CallExpr& call) {
    if (call.arguments.size() < 2 || !call.arguments[1]) {
        return std::string(kExpressNs) + "Locals()";
    }
    const Expr& options = *call.arguments[1];
    if (effectiveType(options).name == kExpressLocals) {
        return expr(options);
    }
    std::string sets;
    std::string recv;
    if (options.kind == NodeKind::ObjectLiteral) {
        const auto& literal = static_cast<const ObjectLiteral&>(options);
        for (const auto& propertyPtr : literal.properties) {
            const ObjectProperty* property = propertyPtr.get();
            if (property == nullptr || !property->value) continue;
            if (property->isMethod || !property->key) {
                reportTypeError(
                    options,
                    "type mismatch: res.render options cannot contain a "
                    "method");
                return "0.0";
            }
            if (property->value->kind == NodeKind::SpreadElement) {
                reportTypeError(
                    options,
                    "type mismatch: res.render options cannot contain a "
                    "spread element");
                return "0.0";
            }
            std::string key;
            if (property->computed) {
                key = expr(*property->key);
            } else if (property->key->kind == NodeKind::Identifier) {
                key = cppStringLiteral(
                    static_cast<const Identifier&>(*property->key).name);
            } else if (property->key->kind == NodeKind::StringLiteral) {
                key = cppStringLiteral(
                    static_cast<const StringLiteral&>(*property->key).value);
            } else {
                reportTypeError(
                    options,
                    "type mismatch: res.render options keys must be names "
                    "or strings");
                return "0.0";
            }
            sets += " cp3_options.set(" + key + ", " +
                    expressSettingValueText(*property->value) + ");";
        }
    } else {
        // A plain object-shape value (`const opts = { title: "x" }`): its
        // primitive fields become the entries.
        Type object;
        bool optional = false;
        if (!objectReceiver(effectiveType(options), object, optional) ||
            optional) {
            reportTypeError(
                options,
                "type mismatch: res.render options must be an object "
                "literal");
            return "0.0";
        }
        std::string structName;
        std::vector<ObjectField> fields;
        if (!objectShapeOf(object, structName, fields)) {
            reportTypeError(
                options,
                "type mismatch: res.render options must be an object "
                "literal");
            return "0.0";
        }
        recv = expr(options);
        for (const ObjectField& field : fields) {
            const CppInfo info = classify(field.type);
            if (!info.representable ||
                (info.shape != Shape::String && info.shape != Shape::Number &&
                 info.shape != Shape::Bool)) {
                reportTypeError(
                    options,
                    "type mismatch: res.render options field '" + field.name +
                        "' is not a string, number, or boolean");
                return "0.0";
            }
            sets += " cp3_options.set(" + cppStringLiteral(field.name) +
                    ", " + std::string(kExpressNs) + "SettingValue((" + recv +
                    ")->" + mapIdent(field.name) + "));";
        }
    }
    return "([&]{ " + std::string(kExpressNs) + "Locals cp3_options;" + sets +
           " return cp3_options; }())";
}

/// One argument of an Express settings call; an arrow (the `app.engine`
/// handler) becomes a lambda with the runtime callback's parameter types.
std::string Generator::expressSettingsArg(const CallExpr& call,
                                          std::size_t index) {
    const Expr& arg = *call.arguments[index];
    if (arg.kind != NodeKind::ArrowFunction) return expr(arg);
    return emitArrow(
        static_cast<const ArrowFunction&>(arg),
        {std::string("std::string"),
         std::string("const ::cp3::runtime::express::Locals&")});
}

std::string Generator::expressSettingsArgs(const CallExpr& call) {
    std::string args;
    for (std::size_t i = 0; i < call.arguments.size(); ++i) {
        if (!call.arguments[i]) continue;
        if (!args.empty()) args += ", ";
        args += expressSettingsArg(call, i);
    }
    return args;
}

std::string Generator::emitExpressSettingsMemberCall(const CallExpr& c,
                                                      const MemberExpr& m) {
    if (!m.object) return std::string();
    const Type receiver = effectiveType(*m.object);
    const std::string recv = expr(*m.object);

    if (receiver.name == kExpressApp) {
        needsExpressSettings_ = true;
        if (m.property == "get") {
            if (c.arguments.size() != 1 || !c.arguments[0]) {
                reportCodegenError(
                    c, diagcode::kArityMismatch,
                    "arity mismatch: app.get(name) takes exactly one argument");
                return "0.0";
            }
            return "(" + recv + ").getSetting(" + expr(*c.arguments[0]) + ")";
        }
        if (m.property == "set") {
            if (c.arguments.size() != 2 || !c.arguments[0] ||
                !c.arguments[1]) {
                reportCodegenError(
                    c, diagcode::kArityMismatch,
                    "arity mismatch: app.set(name, value) takes exactly two "
                    "arguments");
                return "0.0";
            }
            return "(" + recv + ").setSetting(" + expr(*c.arguments[0]) +
                   ", " + expressSettingValueText(*c.arguments[1]) + ")";
        }
        if (m.property == "enable" || m.property == "disable" ||
            m.property == "enabled") {
            return "(" + recv + ")." + m.property + "(" +
                   expressSettingsArgs(c) + ")";
        }
        if (m.property == "engine") {
            if (c.arguments.size() != 2 || !c.arguments[0] ||
                !c.arguments[1]) {
                reportCodegenError(
                    c, diagcode::kArityMismatch,
                    "arity mismatch: app.engine(ext, handler) takes exactly "
                    "two arguments");
                return "0.0";
            }
            return "(" + recv + ").setEngine(" + expressSettingsArg(c, 0) +
                   ", " + expressSettingsArg(c, 1) + ")";
        }
        if (m.property == "render") {
            if (c.arguments.empty() || !c.arguments[0]) {
                reportCodegenError(
                    c, diagcode::kArityMismatch,
                    "arity mismatch: app.render(view, options?) needs a view");
                return "0.0";
            }
            return "(" + recv + ").renderToString(" +
                   expr(*c.arguments[0]) + ", " +
                   expressRenderOptionsText(c) + ")";
        }
        return std::string();
    }

    if (receiver.name == kHttpResponse && m.property == "render") {
        needsExpressSettings_ = true;
        if (c.arguments.empty() || !c.arguments[0]) {
            reportCodegenError(
                c, diagcode::kArityMismatch,
                "arity mismatch: res.render(view, options?) needs a view");
            return "0.0";
        }
        return std::string(kExpressNs) + "renderResponse(" + recv + ", " +
               expr(*c.arguments[0]) + ", " + expressRenderOptionsText(c) +
               ")";
    }

    if (receiver.name == kExpressLocals) {
        needsExpressSettings_ = true;
        if (m.property == "get") {
            return "(" + recv + ").getOr(" + expressSettingsArgs(c) + ")";
        }
        if (m.property == "set") {
            if (c.arguments.size() != 2 || !c.arguments[0] ||
                !c.arguments[1]) {
                reportCodegenError(
                    c, diagcode::kArityMismatch,
                    "arity mismatch: locals.set(name, value) takes exactly "
                    "two arguments");
                return "0.0";
            }
            return "(" + recv + ").set(" + expr(*c.arguments[0]) + ", " +
                   expressSettingValueText(*c.arguments[1]) + ")";
        }
        if (m.property == "has") {
            return "(" + recv + ").has(" + expressSettingsArgs(c) + ")";
        }
        return std::string();
    }

    return std::string();
}

std::string Generator::emitExpressSettingsMemberRead(const MemberExpr& m) {
    if (!m.object) return std::string();
    const Type receiver = effectiveType(*m.object);

    if (receiver.name == kExpressApp) {
        if (m.property == "locals" || m.property == "settings") {
            needsExpressSettings_ = true;
            return "(" + expr(*m.object) + ")." + m.property;
        }
        return std::string();
    }
    if (receiver.name == kHttpResponse && m.property == "locals") {
        needsExpressSettings_ = true;
        return std::string(kExpressNs) + "resLocalsFor(" + expr(*m.object) +
               ")";
    }
    if (receiver.name == kHttpRequest && m.property == "protocol") {
        needsExpressSettings_ = true;
        return std::string(kExpressNs) + "requestProtocol(" +
               expr(*m.object) + ")";
    }
    if (receiver.name == kExpressLocals) {
        // `app.locals.title` / `res.locals.title`: a missing key reads as
        // null (Locals::getOr), never throws.
        needsExpressSettings_ = true;
        return "(" + expr(*m.object) + ").getOr(" +
               cppStringLiteral(m.property) + ")";
    }
    return std::string();
}

std::string Generator::emitExpressLocalsIndexRead(const IndexExpr& ix) {
    if (!ix.object || !ix.index) return std::string();
    if (effectiveType(*ix.object).name != kExpressLocals) {
        return std::string();
    }
    needsExpressSettings_ = true;
    return "(" + expr(*ix.object) + ").getOr(" + expr(*ix.index) + ")";
}

std::string Generator::emitExpressLocalsAssign(const AssignmentExpr& a,
                                               const MemberExpr& m) {
    if (!m.object || !a.value) return std::string();
    if (effectiveType(*m.object).name != kExpressLocals) return std::string();
    if (a.op != AssignOp::Assign) {
        reportCodegenError(
            a, diagcode::kInvalidAssignmentTarget,
            "invalid assignment target: a locals entry only supports plain "
            "assignment (read, modify, then assign instead)");
        return "0.0";
    }
    needsExpressSettings_ = true;
    return "(" + expr(*m.object) + ").set(" + cppStringLiteral(m.property) +
           ", " + expressSettingValueText(*a.value) + ")";
}

std::string Generator::emitExpressLocalsIndexWrite(const AssignmentExpr& a) {
    if (a.target->kind != NodeKind::IndexExpr || !a.value) {
        return std::string();
    }
    const auto& ix = static_cast<const IndexExpr&>(*a.target);
    if (!ix.object || !ix.index) return std::string();
    if (effectiveType(*ix.object).name != kExpressLocals) return std::string();
    if (a.op != AssignOp::Assign) {
        reportCodegenError(
            a, diagcode::kInvalidAssignmentTarget,
            "invalid assignment target: a locals entry only supports plain "
            "assignment (read, modify, then assign instead)");
        return "0.0";
    }
    needsExpressSettings_ = true;
    return "(" + expr(*ix.object) + ").set(" + expr(*ix.index) + ", " +
           expressSettingValueText(*a.value) + ")";
}

}  // namespace cp3
