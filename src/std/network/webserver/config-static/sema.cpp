// =============================================================================
// src/std/network/webserver/config-static/sema.cpp - createServer() config
// (static + body limits) typing: the options-object schema.
//
// OWNERSHIP: the std/network/webserver/config-static family
// (src/std/network/webserver/config-static/**). Wirable fragment in the
// settings-family style: it defines
// Analyzer::checkCreateServerConfigOptions, which the integrator declares
// in detail/analyzer.h and calls from the two existing createServer call
// sites (tryInferNetworkCall / tryInferNetworkMemberCall) INSTEAD of
// checkCreateServerOptions (WIRING.md). It preserves the old threads-only
// diagnostics and adds the config-static keys of config_static.hpp (the
// sema_codegen tables there are the runtime mirror of the key lists below;
// keep them in sync).
//
// Return contract matches checkCreateServerOptions: true when scaled
// (multi-loop) mode is required, i.e. a threads value other than the
// literal 1 was supplied. Everything else only reports diagnostics;
// value-range checks (negative limits, empty static.dir) are the runtime's
// (config_static::validateConfig), because computed values reach it
// through codegen unchecked.
// =============================================================================
#include "detail/analyzer.h"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace {

/// A numeric literal, including a negated one (`-1` parses as a unary
/// expression, not a NumberLiteral). Same shape as the network family's
/// literalThreadCount (kept local: this TU must not depend on that file's
/// anonymous namespace).
bool literalNumberValue(const Expr& e, double& out) {
    if (e.kind == NodeKind::NumberLiteral) {
        out = static_cast<const NumberLiteral&>(e).value;
        return true;
    }
    if (e.kind == NodeKind::UnaryExpr) {
        const auto& unary = static_cast<const UnaryExpr&>(e);
        if (unary.op == UnaryOp::Negate && unary.operand &&
            unary.operand->kind == NodeKind::NumberLiteral) {
            out = -static_cast<const NumberLiteral&>(*unary.operand).value;
            return true;
        }
    }
    return false;
}

/// True when `e` is a scalar literal of a kind the option does not accept
/// (a computed value passes: the runtime validates it).
bool literalKindMismatch(const Expr* e, bool wantNumber, bool wantString,
                         bool wantBool) {
    if (e == nullptr) return false;
    double number = 0.0;
    if (wantNumber && literalNumberValue(*e, number)) return false;
    if (wantString && e->kind == NodeKind::StringLiteral) return false;
    if (wantBool && e->kind == NodeKind::BoolLiteral) return false;
    return e->kind == NodeKind::NumberLiteral ||
           e->kind == NodeKind::StringLiteral ||
           e->kind == NodeKind::BoolLiteral ||
           e->kind == NodeKind::UnaryExpr;
}

struct SubKeyRule {
    const char* key;
    bool wantNumber;
    bool wantString;
    bool wantBool;
    bool wantArray;
};

bool subKeyAllowed(const std::vector<SubKeyRule>& rules, const std::string& key,
                   const SubKeyRule*& rule) {
    for (const SubKeyRule& candidate : rules) {
        if (key == candidate.key) {
            rule = &candidate;
            return true;
        }
    }
    return false;
}

}  // namespace

bool Analyzer::checkCreateServerConfigOptions(CallExpr& call) {
    inferArgs(call);
    if (call.arguments.empty()) return false;
    if (call.arguments.size() > 1 || !call.arguments.front()) {
        report(diagcode::kTypeMismatch, anchorSpan(call.span),
               "createServer() takes at most one argument (an options object: "
               "{ threads?, static?, bodyLimit?, json?, urlencoded?, "
               "trustProxy?, timeouts? })");
        return false;
    }
    const Expr& options = *call.arguments.front();
    if (options.kind != NodeKind::ObjectLiteral) {
        report(diagcode::kTypeMismatch, anchorSpan(options.span),
               "createServer() options must be an object literal: "
               "{ threads?, static?: { dir, index?, etag?, maxAge?, "
               "fallthrough? }, bodyLimit?, json?: { limit?, strict? }, "
               "urlencoded?: { extended? }, trustProxy?, timeouts? }");
        return false;
    }
    bool scale = false;
    // Validates one nested options object (`static`, `json`, ...) against
    // its sub-key table. A non-literal value (identifier, call) is accepted
    // and runtime-checked, matching the threads rule below.
    const auto checkSubObject =
        [this](const Expr* value, const char* group,
               const std::vector<SubKeyRule>& rules) {
            if (value == nullptr) return;
            if (value->kind != NodeKind::ObjectLiteral) {
                const Type valueType = typeOfExpr(*value);
                if (valueType.kind == TypeKind::Unknown ||
                    valueType.kind == TypeKind::Error ||
                    valueType.kind == TypeKind::Object) {
                    return;  // a computed options object: runtime-checked
                }
                report(diagcode::kTypeMismatch, anchorSpan(value->span),
                       std::string("createServer() option '") + group +
                           "' must be an object literal");
                return;
            }
            const auto& literal = static_cast<const ObjectLiteral&>(*value);
            for (const auto& propPtr : literal.properties) {
                const ObjectProperty* prop = propPtr.get();
                if (prop == nullptr) continue;
                if (prop->computed || prop->key == nullptr) {
                    report(diagcode::kTypeMismatch, anchorSpan(prop->span),
                           std::string("createServer() '") + group +
                               "' options only support literal keys");
                    continue;
                }
                const std::string key = keyName(prop->key.get());
                const SubKeyRule* rule = nullptr;
                if (!subKeyAllowed(rules, key, rule)) {
                    report(diagcode::kTypeMismatch, anchorSpan(prop->span),
                           std::string("unknown createServer() '") + group +
                               "' option '" + key + "'");
                    continue;
                }
                if (prop->value == nullptr) continue;
                const bool mismatch =
                    literalKindMismatch(prop->value.get(), rule->wantNumber,
                                        rule->wantString, rule->wantBool) ||
                    (rule->wantArray &&
                     prop->value->kind != NodeKind::ArrayLiteral &&
                     !literalKindMismatch(prop->value.get(), false, false, false));
                if (mismatch) {
                    report(diagcode::kTypeMismatch,
                           anchorSpan(prop->value->span),
                           std::string("createServer() '") + group + "." + key +
                               "' has the wrong literal type");
                }
            }
        };

    static const std::vector<SubKeyRule> kStaticRules = {
        {"dir", false, true, false, false},
        {"index", false, true, true, false},
        {"etag", false, false, true, false},
        {"maxAge", true, false, false, false},
        {"fallthrough", false, false, true, false},
    };
    static const std::vector<SubKeyRule> kJsonRules = {
        {"limit", true, true, false, false},
        {"strict", false, false, true, false},
    };
    static const std::vector<SubKeyRule> kUrlEncodedRules = {
        {"extended", false, false, true, false},
        {"limit", true, true, false, false},
    };
    static const std::vector<SubKeyRule> kTimeoutRules = {
        {"request", true, false, false, false},
        {"headers", true, false, false, false},
        {"idle", true, false, false, false},
        {"keepAlive", true, false, false, false},
    };

    const auto& literal = static_cast<const ObjectLiteral&>(options);
    for (const auto& propPtr : literal.properties) {
        const ObjectProperty* prop = propPtr.get();
        if (prop == nullptr) continue;
        if (prop->computed || prop->key == nullptr) {
            report(diagcode::kTypeMismatch, anchorSpan(prop->span),
                   "createServer() options only support literal keys");
            continue;
        }
        const std::string key = keyName(prop->key.get());
        const Expr* value = prop->value.get();
        if (key == "threads") {
            if (value == nullptr) continue;
            double literalCount = 0.0;
            if (literalNumberValue(*value, literalCount)) {
                if (!std::isfinite(literalCount) || literalCount < 1.0 ||
                    literalCount != std::floor(literalCount)) {
                    report(diagcode::kTypeMismatch, anchorSpan(value->span),
                           "createServer() 'threads' must be an integer >= 1");
                } else if (literalCount != 1.0) {
                    scale = true;
                }
                continue;
            }
            // A computed value may still be known to be non-numeric;
            // otherwise it is accepted and checked by the runtime.
            const Type valueType = typeOfExpr(*value);
            const bool numericLiteral =
                valueType.kind == TypeKind::Literal &&
                valueType.literalBaseType().kind == TypeKind::Number;
            if (valueType.kind == TypeKind::Number || numericLiteral ||
                valueType.kind == TypeKind::Unknown ||
                valueType.kind == TypeKind::Error) {
                scale = true;
            } else {
                report(diagcode::kTypeMismatch, anchorSpan(value->span),
                       "createServer() 'threads' must be a number");
            }
            continue;
        }
        if (key == "static") {
            checkSubObject(value, "static", kStaticRules);
            continue;
        }
        if (key == "json") {
            checkSubObject(value, "json", kJsonRules);
            continue;
        }
        if (key == "urlencoded") {
            checkSubObject(value, "urlencoded", kUrlEncodedRules);
            continue;
        }
        if (key == "timeouts") {
            checkSubObject(value, "timeouts", kTimeoutRules);
            continue;
        }
        if (key == "bodyLimit") {
            if (value != nullptr &&
                literalKindMismatch(value, true, true, false)) {
                report(diagcode::kTypeMismatch, anchorSpan(value->span),
                       "createServer() 'bodyLimit' must be a number or a "
                       "bytes string (\"100kb\")");
            }
            continue;
        }
        if (key == "trustProxy") {
            if (value != nullptr && value->kind == NodeKind::ArrayLiteral) {
                continue;  // string[] form
            }
            if (value != nullptr &&
                literalKindMismatch(value, true, true, true)) {
                report(diagcode::kTypeMismatch, anchorSpan(value->span),
                       "createServer() 'trustProxy' must be a boolean, "
                       "number, string, or string array");
            }
            continue;
        }
        report(diagcode::kTypeMismatch, anchorSpan(prop->span),
               "unknown createServer() option '" + key +
                   "' (supported: threads, static, bodyLimit, json, "
                   "urlencoded, trustProxy, timeouts)");
    }
    return scale;
}

}  // namespace cp3
