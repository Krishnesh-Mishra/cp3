// =============================================================================
// src/std/network/webserver/config-static/codegen.cpp - createServer() config
// (static + body limits) lowering.
//
// OWNERSHIP: the std/network/webserver/config-static family
// (src/std/network/webserver/config-static/**). Defines
// Generator::emitConfigStaticCreateServer, which the integrator declares in
// generator.h and tries at the top of the createServer arm of
// emitNetworkIdentifier (before the threads-only lowering) - WIRING.md.
// It returns "" for everything it does not own (no argument, a
// non-literal options object, or an object with no config-static key), so
// the legacy threads lowering keeps handling those programs unchanged.
// The runtime target is config_static::createServerWithConfig (one call:
// validate + construct + install the middleware chain, see the header).
// =============================================================================
#include "detail/generator/generator.h"

#include <string>
#include <vector>

namespace cp3 {

namespace {

constexpr const char* kConfigStaticNs =
    "::cp3::runtime::network::webserver::config_static::";
constexpr const char* kBodyParseNs =
    "::cp3::runtime::network::webserver::body_parse::";
constexpr const char* kwebserverNs = "::cp3::runtime::webserver::";

/// The literal property name of an object-literal entry ("" when computed).
/// Local copy of the network codegen unit's objectKey (that one is not
/// declared in a header); same spelling, so the walker matches it exactly.
std::string objectKey(const ObjectProperty& p) {
    if (p.computed || !p.key) return std::string();
    if (p.key->kind == NodeKind::Identifier) {
        return static_cast<const Identifier&>(*p.key).name;
    }
    if (p.key->kind == NodeKind::StringLiteral) {
        return static_cast<const StringLiteral&>(*p.key).value;
    }
    return std::string();
}

bool isConfigStaticKey(const std::string& key) {
    return key == "static" || key == "bodyLimit" || key == "json" ||
           key == "urlencoded" || key == "trustProxy" || key == "timeouts";
}

/// `limit` value text: a string literal is a bytes.js string ("100kb"),
/// anything else is already a byte count (sema_codegen::limitValueText).
std::string limitText(Generator& gen, const Expr& value) {
    if (value.kind == NodeKind::StringLiteral) {
        return std::string(kBodyParseNs) + "parseLimitText(" + gen.expr(value) + ")";
    }
    return gen.expr(value);
}

/// `trustProxy` value text: the settings SettingValue carrier
/// (bool | number | string | string[]); a string-array literal becomes a
/// std::vector<std::string> (same shape as the settings family's
/// webserverSettingValueText).
std::string trustProxyText(Generator& gen, const Expr& value) {
    if (value.kind == NodeKind::ArrayLiteral) {
        const auto& literal = static_cast<const ArrayLiteral&>(value);
        std::string items;
        for (const auto& elementPtr : literal.elements) {
            const Expr* element = elementPtr.get();
            if (element == nullptr || element->kind == NodeKind::SpreadElement) {
                gen.reportTypeError(
                    value,
                    "type mismatch: a trustProxy string list cannot contain a "
                    "spread element");
                return "0.0";
            }
            if (!items.empty()) items += ", ";
            items += gen.expr(*element);
        }
        return std::string(kwebserverNs) +
               "SettingValue(std::vector<std::string>{" + items + "})";
    }
    return std::string(kwebserverNs) + "SettingValue(" + gen.expr(value) + ")";
}

}  // namespace

std::string Generator::emitConfigStaticCreateServer(const CallExpr& c) {
    if (c.arguments.empty() || !c.arguments.front()) return "";
    const Expr* options = c.arguments.front().get();
    if (options->kind != NodeKind::ObjectLiteral) return "";  // legacy path
    const auto& literal = static_cast<const ObjectLiteral&>(*options);

    bool hasConfigStatic = false;
    for (const auto& prop : literal.properties) {
        if (prop && !prop->computed && prop->key &&
            isConfigStaticKey(objectKey(*prop))) {
            hasConfigStatic = true;
            break;
        }
    }
    if (!hasConfigStatic) return "";  // threads-only: legacy lowering

    needsNetwork_ = true;
    needsConfigStatic_ = true;

    std::string inner;
    const auto subObject = [this, &inner](const ObjectProperty& prop,
                                          const char* group) {
        // Walks one nested options object and appends field assignments.
        const Expr* sub = prop.value.get();
        if (sub == nullptr) return;
        if (sub->kind != NodeKind::ObjectLiteral) {
            reportTypeError(*sub, std::string("type mismatch: createServer() '") +
                                      group + "' must be an object literal");
            return;
        }
        const auto& subLiteral = static_cast<const ObjectLiteral&>(*sub);
        for (const auto& subProp : subLiteral.properties) {
            if (!subProp || !subProp->value) continue;
            const std::string key = objectKey(*subProp);
            const Expr& value = *subProp->value;
            const std::string groupName = group;
            if (groupName == "static") {
                if (key == "dir") inner += ".static_.dir = " + expr(value) + "; ";
                else if (key == "index") {
                    if (value.kind == NodeKind::BoolLiteral) {
                        inner += ".static_.serveIndex = " +
                                 std::string(static_cast<const BoolLiteral&>(value).value
                                                 ? "true"
                                                 : "false") +
                                 "; ";
                    } else {
                        inner += ".static_.serveIndex = true; .static_.index = " +
                                 expr(value) + "; ";
                    }
                } else if (key == "etag") {
                    inner += ".static_.etag = " + expr(value) + "; ";
                } else if (key == "maxAge") {
                    inner += ".static_.maxAge = " + expr(value) + "; ";
                } else if (key == "fallthrough") {
                    inner += ".static_.fallthrough = " + expr(value) + "; ";
                } else {
                    reportTypeError(value, "type mismatch: unknown createServer() "
                                           "'static' option '" +
                                               key + "'");
                }
            } else if (groupName == "json") {
                if (key == "limit") {
                    inner += ".json.hasLimit = true; .json.limitBytes = " +
                             limitText(*this, value) + "; ";
                } else if (key == "strict") {
                    inner += ".json.strict = " + expr(value) + "; ";
                } else {
                    reportTypeError(value, "type mismatch: unknown createServer() "
                                           "'json' option '" +
                                               key + "'");
                }
            } else if (groupName == "urlencoded") {
                if (key == "extended") {
                    inner += ".urlencoded.extended = " + expr(value) + "; ";
                } else if (key == "limit") {
                    inner += ".urlencoded.hasLimit = true; "
                             ".urlencoded.limitBytes = " +
                             limitText(*this, value) + "; ";
                } else {
                    reportTypeError(value, "type mismatch: unknown createServer() "
                                           "'urlencoded' option '" +
                                               key + "'");
                }
            } else if (groupName == "timeouts") {
                const char* field = nullptr;
                if (key == "request") field = ".timeouts.requestMs";
                else if (key == "headers") field = ".timeouts.headersMs";
                else if (key == "idle") field = ".timeouts.idleMs";
                else if (key == "keepAlive") field = ".timeouts.keepAliveMs";
                if (field != nullptr) {
                    inner += std::string(field) + " = " + expr(value) + "; ";
                } else {
                    reportTypeError(value, "type mismatch: unknown createServer() "
                                           "'timeouts' option '" +
                                               key + "'");
                }
            }
        }
    };

    for (const auto& prop : literal.properties) {
        if (!prop || !prop->value) continue;
        const std::string key = objectKey(*prop);
        const Expr& value = *prop->value;
        if (key == "threads") {
            inner += ".threads = " + expr(value) + "; ";
        } else if (key == "bodyLimit") {
            inner += ".hasBodyLimit = true; .bodyLimitBytes = " +
                     limitText(*this, value) + "; ";
        } else if (key == "static") {
            inner += ".static_.enabled = true; ";
            subObject(*prop, "static");
        } else if (key == "json") {
            inner += ".json.enabled = true; ";
            subObject(*prop, "json");
        } else if (key == "urlencoded") {
            inner += ".urlencoded.enabled = true; ";
            subObject(*prop, "urlencoded");
        } else if (key == "trustProxy") {
            inner += ".hasTrustProxy = true; .trustProxy = " +
                     trustProxyText(*this, value) + "; ";
        } else if (key == "timeouts") {
            subObject(*prop, "timeouts");
        } else {
            reportTypeError(value, "type mismatch: unknown createServer() option '" +
                                       key + "'");
        }
    }

    return std::string(kConfigStaticNs) +
           "createServerWithConfig([&] { " + std::string(kConfigStaticNs) +
           "CreateServerConfig c; " + inner + "return c; }())";
}

}  // namespace cp3
