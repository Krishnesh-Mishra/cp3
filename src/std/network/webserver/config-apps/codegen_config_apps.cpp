// =============================================================================
// src/std/network/webserver/config-apps/codegen_config_apps.cpp - createServer
// config lowering (codegen fragment).
//
// OWNERSHIP: src/std/network/webserver/config-apps/** only. Wirable fragment
// against the Generator vocabulary (detail/generator/generator.h); splice
// lines at the bottom (see README.md). The runtime target is
// cp3::runtime::network::webserver::configapps (config_apps.hpp).
//
// Lowering rules:
//   * createServer() / createServer({ threads }) -> UNCHANGED (existing
//     emitNetworkIdentifier arm; this fragment only claims the call when the
//     options object carries any config key: views/middleware/settings/
//     locals/onError).
//   * createServer({ views, middleware, settings, locals, onError })
//       -> makeConfiguredApp(ServerConfig{...}) built property by property:
//       views.dir/engine    -> Settings "views" / "view engine" (runtime)
//       views.engineFn      -> ViewsConfig::engineFn (arrow context
//                              (string, const Locals&) -> string, settings
//                              family shape); a non-string views.engine
//                              (e.g. an arrow) is a type error here: the
//                              extension must be spelled by `engine`.
//       settings.*          -> AppSettingsConfig fields (etag/trustProxy as
//                              SettingValue, xPoweredBy as bool, jsonSpaces
//                              as a number; string[] via SettingValue list)
//       locals              -> Locals bag, entry by entry (settings family
//                              webserverSettingValueText shapes)
//       middleware: [...]   -> MiddlewareEntry::normal / ::error per element
//                              arity (3 = normal, 4 = error; other arities
//                              are reported), in array order
//       onError             -> ConfigErrorFn; 3-arg arrows are adapted with
//                              configapps::onErrorFrom3Arg
//     Non-literal option values (a `const cfg = {...}` binding, computed
//     keys) are a type error at the use site: the config object must be an
//     object literal, exactly like `threads` today. Unknown keys are type
//     errors (sema reports them; codegen ignores them defensively).
//   * ConfiguredApp member calls (emitConfigMemberCall): verbs / route /
//     param / listen / close forward to the HttpServer members; use() /
//     useError() forward to the middleware engine; set/get/enable/disable/
//     enabled/engine/render forward to the settings app members; locals /
//     settings reads return (recv).locals() / (recv).settings().
//     NOTE: `app.locals.<k>` reads/writes and `res.render` are lowered by
//     the SETTINGS family's hooks (they target the settings webserverApp
//     type); for a ConfiguredApp receiver this fragment's member-read arm
//     returns `(recv).locals()` so those hooks never see the receiver.
//     `app.get(path, h)` stays ambiguous with `app.get(name)` only in
//     spelling: the verb rows are recognised by a string + handler shape
//     here, the setting row by a single string argument.
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

constexpr const char* kConfigNs =
    "::cp3::runtime::network::webserver::configapps::";
constexpr const char* kSettingsNs = "::cp3::runtime::webserver::";
constexpr const char* kConfiguredAppMarker = "$webserver:ConfiguredApp";

constexpr const char* kReqType = "::cp3::runtime::network::HttpRequest&";
constexpr const char* kResType = "::cp3::runtime::network::HttpResponse&";
constexpr const char* kNextType =
    "::cp3::runtime::network::webserver::webserverNext&";
constexpr const char* kThrownType = "::cp3::runtime::Thrown";

/// Object-literal key text, or "" for computed/method properties.
std::string literalKey(const ObjectProperty& property) {
    if (property.computed || property.isMethod || !property.key) return "";
    if (property.key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*property.key).name;
    }
    if (property.key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*property.key).value;
    }
    return "";
}

bool isConfigKey(const std::string& key) {
    return key == "views" || key == "middleware" || key == "settings" ||
           key == "locals" || key == "onError";
}

}  // namespace

// The SettingValue spelling of one config scalar (settings family shape):
// string | number | boolean | null | string[]; anything else is a type
// error at the use site, not a silent stringification.
std::string Generator::configSettingValueText(const Expr& value) {
    if (value.kind == NodeKind::UndefinedLiteral) {
        return std::string(kSettingsNs) + "SettingValue()";
    }
    if (value.kind == NodeKind::ArrayLiteral) {
        const auto& literal = static_cast<const ArrayLiteral&>(value);
        std::vector<std::string> items;
        for (const auto& elementPtr : literal.elements) {
            const Expr* element = elementPtr.get();
            if (element == nullptr ||
                element->kind == NodeKind::SpreadElement) {
                reportTypeError(value,
                                "type mismatch: a config string list cannot "
                                "contain a spread element");
                return "0.0";
            }
            items.push_back(expr(*element));
        }
        return std::string(kSettingsNs) +
               "SettingValue(std::vector<std::string>{" +
               joinStrings(items, ", ") + "})";
    }
    return std::string(kSettingsNs) + "SettingValue(" + expr(value) + ")";
}

// One arrow lowered with an explicit parameter-type context (sync or async
// coroutine shape, mirroring the routing fragment's routingHandler).
std::string Generator::configArrow(const Expr& arg,
                                   const std::vector<std::string>& context) {
    if (arg.kind != NodeKind::ArrowFunction) return expr(arg);
    const auto& arrow = static_cast<const ArrowFunction&>(arg);
    std::string text;
    if (arrow.isAsync) {
        std::optional<Type> expectedFn;
        if (expected_ && expected_->kind == TypeKind::Function) {
            expectedFn = *expected_;
        }
        ++nestingDepth_;
        pushScope();
        const std::vector<std::string> params =
            arrowParamText(arrow, context, expectedFn);
        std::string signature;
        const std::string body = arrowBodyText(arrow, true, expectedFn, signature);
        popScope();
        --nestingDepth_;
        text = "[&](" + joinStrings(params, ", ") + ") -> " + signature +
               " {\n" + body + "}";
    } else {
        text = emitArrow(arrow, context);
    }
    return text;
}

// `views: { dir?, engine?, engineFn? }` -> ViewsConfig assignments.
std::string Generator::configViewsText(const Expr& value) {
    if (value.kind != NodeKind::ObjectLiteral) {
        reportTypeError(value,
                        "type mismatch: createServer() 'views' must be an "
                        "object literal: { dir?, engine?, engineFn? }");
        return "0.0";
    }
    std::string out;
    const auto& literal = static_cast<const ObjectLiteral&>(value);
    for (const auto& propertyPtr : literal.properties) {
        const ObjectProperty* property = propertyPtr.get();
        if (property == nullptr || !property->value) continue;
        const std::string key = literalKey(*property);
        if (key == "dir" || key == "engine") {
            if (property->value->kind == NodeKind::ArrowFunction) {
                reportTypeError(
                    *property->value,
                    "type mismatch: createServer() 'views." + key +
                        "' must be a string" +
                        (key == "engine"
                             ? " (the extension); pass the function as "
                               "'views.engineFn'"
                             : ""));
                continue;
            }
            out += " cp3_cfg.views." + key + " = " + expr(*property->value) +
                   ";";
        } else if (key == "engineFn") {
            out += " cp3_cfg.views.engineFn = " +
                   configArrow(*property->value,
                               {std::string("std::string"),
                                std::string("const ::cp3::runtime::webserver::"
                                            "Locals&")}) +
                   ";";
        } else if (!key.empty()) {
            reportTypeError(
                *property,
                "type mismatch: unknown createServer() 'views' option '" +
                    key + "'");
        }
    }
    return out;
}

// `settings: { etag?, xPoweredBy?, jsonSpaces?, trustProxy? }`.
std::string Generator::configSettingsText(const Expr& value) {
    if (value.kind != NodeKind::ObjectLiteral) {
        reportTypeError(value,
                        "type mismatch: createServer() 'settings' must be "
                        "an object literal: { etag?, xPoweredBy?, "
                        "jsonSpaces?, trustProxy? }");
        return "0.0";
    }
    std::string out;
    const auto& literal = static_cast<const ObjectLiteral&>(value);
    for (const auto& propertyPtr : literal.properties) {
        const ObjectProperty* property = propertyPtr.get();
        if (property == nullptr || !property->value) continue;
        const std::string key = literalKey(*property);
        if (key == "etag" || key == "trustProxy") {
            out += " cp3_cfg.settings." + key + " = " +
                   configSettingValueText(*property->value) + ";";
        } else if (key == "xPoweredBy") {
            out += " cp3_cfg.settings.xPoweredBy = " +
                   expr(*property->value) + ";";
        } else if (key == "jsonSpaces") {
            out += " cp3_cfg.settings.jsonSpaces = " +
                   expr(*property->value) + ";";
        } else if (!key.empty()) {
            reportTypeError(
                *property,
                "type mismatch: unknown createServer() 'settings' option '" +
                    key + "' (supported: etag, xPoweredBy, jsonSpaces, "
                    "trustProxy)");
        }
    }
    return out;
}

// `locals: { ... }` -> Locals entries (later keys win, like a JS object).
std::string Generator::configLocalsText(const Expr& value) {
    if (value.kind != NodeKind::ObjectLiteral) {
        reportTypeError(value,
                        "type mismatch: createServer() 'locals' must be an "
                        "object literal");
        return "0.0";
    }
    std::string out;
    const auto& literal = static_cast<const ObjectLiteral&>(value);
    for (const auto& propertyPtr : literal.properties) {
        const ObjectProperty* property = propertyPtr.get();
        if (property == nullptr || !property->value) continue;
        if (property->isMethod || property->value->kind ==
                                      NodeKind::SpreadElement) {
            reportTypeError(value,
                            "type mismatch: createServer() 'locals' cannot "
                            "contain a method or spread element");
            return "0.0";
        }
        const std::string key = literalKey(*property);
        if (key.empty()) {
            reportTypeError(value,
                            "type mismatch: createServer() 'locals' keys "
                            "must be names or strings");
            return "0.0";
        }
        out += " cp3_cfg.locals.set(" + cppStringLiteral(key) + ", " +
               configSettingValueText(*property->value) + ");";
    }
    return out;
}

// `middleware: [ ... ]` -> MiddlewareEntry list text (arity-classified).
std::string Generator::configMiddlewareText(const Expr& value) {
    if (value.kind != NodeKind::ArrayLiteral) {
        reportTypeError(value,
                        "type mismatch: createServer() 'middleware' must be "
                        "an array of handlers");
        return "0.0";
    }
    const auto& literal = static_cast<const ArrayLiteral&>(value);
    std::vector<std::string> entries;
    for (const auto& elementPtr : literal.elements) {
        const Expr* element = elementPtr.get();
        if (element == nullptr || element->kind == NodeKind::SpreadElement) {
            reportTypeError(value,
                            "type mismatch: createServer() 'middleware' "
                            "cannot contain a spread element");
            return "0.0";
        }
        if (element->kind == NodeKind::ArrowFunction) {
            const auto& arrow = static_cast<const ArrowFunction&>(*element);
            const std::size_t arity = arrow.params.size();
            if (arity == 3) {
                entries.push_back(
                    std::string(kConfigNs) + "MiddlewareEntry::normal(" +
                    configArrow(*element, {kReqType, kResType, kNextType}) +
                    ")");
            } else if (arity == 4) {
                entries.push_back(
                    std::string(kConfigNs) + "MiddlewareEntry::error(" +
                    configArrow(*element,
                                {kThrownType, kReqType, kResType, kNextType}) +
                    ")");
            } else {
                reportTypeError(
                    *element,
                    "type mismatch: a createServer() middleware handler "
                    "takes 3 parameters (req, res, next) or 4 (err, req, "
                    "res, next)");
                return "0.0";
            }
        } else {
            // A named handler value: assumed normal 3-arg middleware.
            entries.push_back(std::string(kConfigNs) +
                              "MiddlewareEntry::normal(" + expr(*element) +
                              ")");
        }
    }
    return "std::vector<" + std::string(kConfigNs) + "MiddlewareEntry>{" +
           joinStrings(entries, ", ") + "}";
}

// `onError: (err, req, res[, next]) => ...`.
std::string Generator::configOnErrorText(const Expr& value) {
    if (value.kind != NodeKind::ArrowFunction) return expr(value);
    const auto& arrow = static_cast<const ArrowFunction&>(value);
    if (arrow.params.size() == 4) {
        return configArrow(value, {kThrownType, kReqType, kResType, kNextType});
    }
    if (arrow.params.size() == 3) {
        return std::string(kConfigNs) + "onErrorFrom3Arg(" +
               configArrow(value, {kThrownType, kReqType, kResType}) + ")";
    }
    reportTypeError(value,
                    "type mismatch: createServer() 'onError' takes 3 "
                    "parameters (err, req, res) or 4 (err, req, res, next)");
    return "0.0";
}

// True when this createServer call carries a config object (any config
// key); pure `{ threads }` / no-arg calls keep the existing lowering.
bool Generator::isCreateServerConfigCall(const CallExpr& c) {
    if (c.arguments.empty() || !c.arguments.front()) return false;
    const Expr& options = *c.arguments.front();
    if (options.kind != NodeKind::ObjectLiteral) return false;
    const auto& literal = static_cast<const ObjectLiteral&>(options);
    for (const auto& propertyPtr : literal.properties) {
        const ObjectProperty* property = propertyPtr.get();
        if (property == nullptr) continue;
        if (isConfigKey(literalKey(*property))) return true;
    }
    return false;
}

// `createServer({ views, middleware, settings, locals, onError })` ->
// makeConfiguredApp(...). "" when this is not a config call.
std::string Generator::emitCreateServerConfig(const CallExpr& c) {
    if (!isCreateServerConfigCall(c)) return std::string();
    needsNetwork_ = true;
    needsEventLoop_ = true;
    const auto& literal =
        static_cast<const ObjectLiteral&>(*c.arguments.front());
    std::string body;
    for (const auto& propertyPtr : literal.properties) {
        const ObjectProperty* property = propertyPtr.get();
        if (property == nullptr || !property->value) continue;
        const std::string key = literalKey(*property);
        if (key == "views") {
            body += configViewsText(*property->value);
        } else if (key == "settings") {
            body += configSettingsText(*property->value);
        } else if (key == "locals") {
            body += configLocalsText(*property->value);
        } else if (key == "middleware") {
            body += " cp3_cfg.middleware = " +
                    configMiddlewareText(*property->value) + ";";
        } else if (key == "onError") {
            body += " cp3_cfg.onError = " +
                    configOnErrorText(*property->value) + ";";
        } else if (key == "threads") {
            // threads + config together: reported by sema as unsupported
            // (the config app owns one single-loop HttpServer); ignore
            // defensively rather than emitting a second server.
            reportTypeError(
                *property,
                "type mismatch: createServer() cannot combine 'threads' "
                "with the views/middleware/settings config");
        } else if (!key.empty()) {
            reportTypeError(*property,
                            "type mismatch: unknown createServer() option '" +
                                key + "'");
        }
    }
    return "([&]{ " + std::string(kConfigNs) + "ServerConfig cp3_cfg;" + body +
           " return " + std::string(kConfigNs) +
           "makeConfiguredApp(std::move(cp3_cfg)); }())";
}

// Member-call dispatch for ConfiguredApp receivers. "" = not ours. Settings
// rows mirror the settings family's lowering onto the forwarding members;
// verb rows forward onto HttpServer (server()); use rows onto the engine.
std::string Generator::emitConfigMemberCall(const CallExpr& c,
                                            const MemberExpr& m) {
    if (!m.object) return std::string();
    if (effectiveType(*m.object).name != kConfiguredAppMarker) {
        return std::string();
    }
    needsNetwork_ = true;
    needsEventLoop_ = true;
    const std::string recv = "(" + expr(*m.object) + ")";

    // Settings / view rows.
    if (m.property == "set" && c.arguments.size() == 2) {
        return recv + ".setSetting(" + expr(*c.arguments[0]) + ", " +
               configSettingValueText(*c.arguments[1]) + ")";
    }
    if (m.property == "get" && c.arguments.size() == 1) {
        // Single string argument: the setting read. The verb form always
        // carries a handler as its second argument, so arity disambiguates.
        return recv + ".getSetting(" + expr(*c.arguments[0]) + ")";
    }
    if (m.property == "enable" || m.property == "disable" ||
        m.property == "enabled") {
        return recv + "." + m.property + "(" +
               (c.arguments.empty() || !c.arguments[0]
                    ? std::string()
                    : expr(*c.arguments[0])) +
               ")";
    }
    if (m.property == "engine" && c.arguments.size() == 2) {
        return recv + ".setEngine(" + expr(*c.arguments[0]) + ", " +
               configArrow(*c.arguments[1],
                           {std::string("std::string"),
                            std::string("const ::cp3::runtime::webserver::"
                                        "Locals&")}) +
               ")";
    }
    if (m.property == "render") {
        // app.render(view, options?) -> renderToString(view, Locals).
        std::string options = std::string(kSettingsNs) + "Locals()";
        if (c.arguments.size() >= 2 && c.arguments[1]) {
            options = configLocalsValue(*c.arguments[1]);
        }
        return recv + ".renderToString(" +
               (c.arguments.empty() || !c.arguments[0]
                    ? std::string("0.0")
                    : expr(*c.arguments[0])) +
               ", " + options + ")";
    }

    // Middleware rows (engine context: webserverNext).
    if (m.property == "use") {
        std::string handler;
        bool errorHandler = false;
        const Expr* fn = nullptr;
        if (c.arguments.size() == 1) fn = c.arguments[0].get();
        if (c.arguments.size() == 2) fn = c.arguments[1].get();
        if (fn == nullptr) return "0.0";
        if (fn->kind == NodeKind::ArrowFunction) {
            const auto& arrow = static_cast<const ArrowFunction&>(*fn);
            errorHandler = arrow.params.size() == 4;
            handler = configArrow(*fn, errorHandler
                                         ? std::vector<std::string>{
                                               kThrownType, kReqType, kResType,
                                               kNextType}
                                         : std::vector<std::string>{
                                               kReqType, kResType, kNextType});
        } else {
            handler = expr(*fn);
        }
        if (c.arguments.size() == 2) {
            return recv + ".use(" + expr(*c.arguments[0]) + ", " + handler +
                   ")";
        }
        (void)errorHandler;
        return recv + ".use(" + handler + ")";
    }
    if (m.property == "onError" && !c.arguments.empty() && c.arguments[0]) {
        return recv + ".onError(" + configOnErrorText(*c.arguments[0]) + ")";
    }

    // HttpServer forwarding rows.
    static const char* kVerbs[] = {"get",   "post", "put",   "delete",
                                   "patch", "head", "options", "any", "all"};
    for (const char* verb : kVerbs) {
        if (m.property == verb) {
            if (c.arguments.size() < 2 || !c.arguments[0] ||
                !c.arguments[1]) {
                reportCodegenError(c, diagcode::kArityMismatch,
                                   m.property +
                                       "(path, handler) takes two arguments");
                return "0.0";
            }
            return recv + "." + m.property + "(" + expr(*c.arguments[0]) +
                   ", " + expr(*c.arguments[1]) + ")";
        }
    }
    if (m.property == "route" && !c.arguments.empty() && c.arguments[0]) {
        return recv + ".route(" + expr(*c.arguments[0]) + ")";
    }
    if (m.property == "param" && c.arguments.size() == 2) {
        return recv + ".param(" + expr(*c.arguments[0]) + ", " +
               expr(*c.arguments[1]) + ")";
    }
    if (m.property == "listen") {
        std::string args;
        for (const auto& arg : c.arguments) {
            if (!arg) continue;
            if (!args.empty()) args += ", ";
            args += expr(*arg);
        }
        return recv + ".listen(" + args + ")";
    }
    if (m.property == "close") return recv + ".close()";
    return std::string();
}

// `app.render(view, options)` options object -> a Locals (same shape as the
// settings family's webserverRenderOptionsText, scoped to this fragment).
std::string Generator::configLocalsValue(const Expr& value) {
    if (value.kind != NodeKind::ObjectLiteral) return expr(value);
    const auto& literal = static_cast<const ObjectLiteral&>(value);
    std::string sets;
    for (const auto& propertyPtr : literal.properties) {
        const ObjectProperty* property = propertyPtr.get();
        if (property == nullptr || !property->value) continue;
        const std::string key = literalKey(*property);
        if (key.empty()) continue;
        sets += " cp3_options.set(" + cppStringLiteral(key) + ", " +
                configSettingValueText(*property->value) + ");";
    }
    return "([&]{ " + std::string(kSettingsNs) + "Locals cp3_options;" + sets +
           " return cp3_options; }())";
}

}  // namespace cp3

// -----------------------------------------------------------------------------
// SPLICE LINES (integrator; see README.md):
//
// 1. Generator declarations (src/codegen/src/detail/generator/generator.h,
//    next to the webserver settings declarations):
//      bool isCreateServerConfigCall(const CallExpr& c);
//      std::string emitCreateServerConfig(const CallExpr& c);
//      std::string emitConfigMemberCall(const CallExpr& c,
//                                       const MemberExpr& m);
//      std::string configSettingValueText(const Expr& value);
//      std::string configViewsText(const Expr& value);
//      std::string configSettingsText(const Expr& value);
//      std::string configLocalsText(const Expr& value);
//      std::string configMiddlewareText(const Expr& value);
//      std::string configOnErrorText(const Expr& value);
//      std::string configLocalsValue(const Expr& value);
//      std::string configArrow(const Expr& arg,
//                              const std::vector<std::string>& context);
//    and generator_state.h (next to needswebserverSettings_):
//      bool needswebserverConfig_ = false;   // set alongside needsNetwork_
//    (optional flag; the config header also pulls in the settings and
//    middleware headers, so needsNetwork_ alone suffices until the
//    integrator wants a finer include gate.)
//
// 2. createServer dispatch (src/std/network/codegen/codegen.cpp,
//    Generator::emitNetworkIdentifier, at the top of the createServer arm,
//    BEFORE the threads extraction):
//      if (std::string cfg = emitCreateServerConfig(c); !cfg.empty())
//          return cfg;
//    and the same line in the `network.createServer` member arm
//    (emitNetworkMemberCall / networkArg createServer arm), so both
//    spellings lower. isNetworkObject must treat the kConfiguredApp marker
//    as a network object receiver (add it next to kHttpServer).
//
// 3. Member-call dispatch (src/codegen/src/emit/std/common.cpp,
//    emitMemberCall, BEFORE the isNetworkObject(...) line, next to the
//    settings hook):
//      if (std::string cfg = emitConfigMemberCall(c, m); !cfg.empty())
//          return cfg;
//    Member READ (emitBuiltinMember, before emitNetworkMemberRead):
//      if (receiver is kConfiguredApp && property == "locals")
//          return "(" + recv + ").locals()";
//      if (receiver is kConfiguredApp && property == "settings")
//          return "(" + recv + ").settings()";
//    (the settings family's read hook must NOT claim a ConfiguredApp
//    receiver: its `(recv).locals` spelling does not exist here.)
//
// 4. Include emission (module_writer.cpp / program.cpp, next to the
//    needsNetwork_ line):
//      out += "#include \"cp3/std/network/webserver/config_apps.hpp\"\n";
//    The config header itself includes the settings + middleware headers.
// -----------------------------------------------------------------------------
