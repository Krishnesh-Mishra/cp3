// =============================================================================
// src/std/network/webserver/regex-routing/codegen_fragment.cpp
//
// CODEGEN FRAGMENT for `src/std/network/codegen/codegen_methods.cpp`
// (Generator methods) — not a TU. Paste the "=== PASTE INTO
// codegen_methods.cpp ===" helpers into the anonymous namespace of that
// file and the dispatch hunks where noted. The file itself is excluded
// from the build (see README.md WIRING).
//
// Lowering contract (what the generated C++ looks like):
//
//   app.get("/users/:id", h)
//     -> (app).get("/users/:id", lambda)                      // unchanged
//
//   app.get(/\/commits\/(\d+)/, h)
//     -> (app).get(::cp3::runtime::network::regex_routing::PathSpec::makeRegex(
//            ::cp3::runtime::regex::Regex::compile("\\/commits\\/(\\d+)", "")),
//            lambda)
//
//   app.get(["/a", "/b"], h)
//     -> (app).get(PathSpec::makeArray({makeString("/a"), makeString("/b")}), lambda)
//
//   app.get(["/a", /\/b\/(\d+)/], h)   // mixed array elementwise
//
//   app.get(path, { caseSensitive: true, strict: true }, h)
//     -> (app).get(pathSpec, RoutingOptions{true, true}, lambda)
//
//   app.set("strict routing", true)  -> (app).set("strict routing", true)
//   app.enable("strict routing")     -> (app).set("strict routing", true)
//   app.get("strict routing")        -> (app).getSetting("strict routing")
//
// The `get`/`set`/`enable`/… spellings need two HttpServer members
// (`getSetting`, `setRoutingSetting`) that forward to the settings
// registry in regex_routing.hpp. ADD THOSE MEMBERS via the WIRING lines
// in README.md (one setter line each); the string-path overload below is
// the existing `HttpServer::get(const std::string&, F)` and stays.
// (nothing above this line is part of the build; the paste blocks below are)

// === PASTE INTO codegen_methods.cpp (anonymous namespace) ==================
//
// #include "cp3/core/regex.hpp"  (for the Regex::compile lowering)
//
// constexpr const char* kRegexRoutingNs =
//     "::cp3::runtime::network::regex_routing::";
// constexpr const char* kRegexLiteralNs = "::cp3::runtime::regex::";
//
// bool isRegexRoutingVerb(const std::string& property) {
//     return property == "get" || property == "post" || property == "put" ||
//            property == "delete" || property == "patch" || property == "head" ||
//            property == "options" || property == "any";
// }
//
// /// The C++ `bool` literal spelled by one option value (a literal
// /// `true`/`false`; a computed webserverion lowers as-is).
// std::string regexRoutingBoolText(const Expr& e) {
//     if (e.kind == NodeKind::BoolLiteral)
//         return static_cast<const BoolLiteral&>(e).value ? "true" : "false";
//     return expr ... // (the Generator's expr(); see the call site below)
// }
//
// /// `RoutingOptions{...}` text for an ObjectLiteral options argument;
// /// unlisted keys stay `std::nullopt` ("inherit the app setting").
// /// `exprOf` is the Generator's `expr` (passed in, like networkClassText).
// template <class ExprFn>
// std::string regexRoutingOptionsText(const Expr& arg, ExprFn exprOf) {
//     if (arg.kind != NodeKind::ObjectLiteral) return "RoutingOptions{}";
//     const auto& lit = static_cast<const ObjectLiteral&>(arg);
//     std::string cs = "std::nullopt", st = "std::nullopt";
//     for (const auto& p : lit.properties) {
//         if (!p || !p->value || p->isMethod || !p->key) continue;
//         std::string key;
//         if (p->key->kind == NodeKind::Identifier)
//             key = static_cast<const Identifier&>(*p->key).name;
//         else if (p->key->kind == NodeKind::StringLiteral)
//             key = static_cast<const StringLiteral&>(*p->key).value;
//         else continue;
//         if (key == "caseSensitive") cs = exprOf(*p->value);
//         else if (key == "strict") st = exprOf(*p->value);
//     }
//     return kRegexRoutingNs + std::string("RoutingOptions{") + cs + ", " + st + "}";
// }
//
// /// One route-path element: string | RegExp | nested array.
// std::string regexRoutingPathElementText(const Expr& e,
//                                         std::function<std::string(const Expr&)>& exprOf) {
//     using namespace... // (see the Generator member below for the real shape)
// }

// --- Generator member helpers (declare in generator.h, define here) --------
std::string Generator::regexRoutingPathText(const Expr& e) {
    if (e.kind == NodeKind::RegExpLiteral) {
        const auto& re = static_cast<const RegExpLiteral&>(e);
        needsRegex_ = true;
        return kRegexRoutingNs + "PathSpec::makeRegex(" + kRegexLiteralNs +
               "Regex::compile(" + cppStringLiteral(re.pattern) + ", " +
               cppStringLiteral(re.flags) + "))";
    }
    if (e.kind == NodeKind::ArrayLiteral) {
        const auto& lit = static_cast<const ArrayLiteral&>(e);
        std::string inner;
        for (const auto& el : lit.elements) {
            if (!el || el->kind == NodeKind::SpreadElement) {
                reportTypeError(e, "type mismatch: a spread element in a route-path "
                                   "array cannot be classified at compile time; "
                                   "spell the array statically");
                continue;
            }
            if (!inner.empty()) inner += ", ";
            inner += regexRoutingPathText(*el);
        }
        return kRegexRoutingNs + "PathSpec::makeArray({" + inner + "})";
    }
    // Anything else (a variable, a call, ...): a string is the only
    // runtime-carriable shape, so wrap it as a string spec.
    return kRegexRoutingNs + "PathSpec::makeString(" + expr(e) + ")";
}

std::string Generator::regexRoutingOptionsCtor(const Expr& arg) {
    if (arg.kind != NodeKind::ObjectLiteral) {
        reportTypeError(arg, "type mismatch: route options must be an object literal "
                             "{ caseSensitive?: boolean, strict?: boolean }");
        return kRegexRoutingNs + "RoutingOptions{}";
    }
    const auto& lit = static_cast<const ObjectLiteral&>(arg);
    std::string cs = "std::nullopt", st = "std::nullopt";
    for (const auto& prop : lit.properties) {
        if (!prop || !prop->value || prop->isMethod || !prop->key) continue;
        std::string key;
        if (prop->key->kind == NodeKind::Identifier)
            key = static_cast<const Identifier&>(*prop->key).name;
        else if (prop->key->kind == NodeKind::StringLiteral)
            key = static_cast<const StringLiteral&>(*prop->key).value;
        else continue;
        if (key == "caseSensitive") cs = expr(*prop->value);
        else if (key == "strict") st = expr(*prop->value);
    }
    return kRegexRoutingNs + "RoutingOptions{" + cs + ", " + st + "}";
}

// === Dispatch hunks in Generator::emitNetworkMemberCall ====================
    // Paste BEFORE the generic `(expr).member(args)` return, after the
    // `cookie` special case and the `json` cases:
    //
    //   if (isRegexRoutingVerb(m.property) &&
    //       effectiveType(*m.object).name == kHttpServer) {
    //       needsNetwork_ = true;
    //       needsEventLoop_ = true;
    //       if (c.arguments.size() == 3 && c.arguments[0] && c.arguments[1] &&
    //           c.arguments[2]) {
    //           // (path, options, handler): the handler keeps the usual
    //           // route context ((req, res) params).
    //           const std::string path = regexRoutingPathText(*c.arguments[0]);
    //           const std::string opts = regexRoutingOptionsCtor(*c.arguments[1]);
    //           const std::string handler =
    //               networkArg(receiver, m.property, c, *c.arguments[2], 2);
    //           return "(" + expr(*m.object) + ").get(" + path + ", " + opts + ", " +
    //                  handler + ")";
    //       }
    //       if (c.arguments.size() == 2) {
    //           // String paths keep today's call shape exactly (no change
    //           // for existing programs); RegExp/array paths need the
    //           // PathSpec overload.
    //           const Expr* first = c.arguments[0].get();
    //           if (first && first->kind != NodeKind::RegExpLiteral &&
    //               first->kind != NodeKind::ArrayLiteral) {
    //               return "(" + expr(*m.object) + ")." +
    //                      networkMemberName(receiver, m.property) + "(" +
    //                      networkArg(receiver, m.property, c, *first, 0) + ", " +
    //                      networkArg(receiver, m.property, c, *c.arguments[1], 1) + ")";
    //           }
    //           // PathSpec form (handler contextual params as usual).
    //           const std::string path = regexRoutingPathText(*first);
    //           const std::string handler =
    //               networkArg(receiver, m.property, c, *c.arguments[1], 1);
    //           return "(" + expr(*m.object) + ").get(" + path + ", " + handler + ")";
    //       }
    //       reportCodegenError(c, diagcode::kArityMismatch,
    //                          "arity mismatch: " + m.property +
    //                              "() takes (path, handler) or (path, options, handler)");
    //       return "0.0";
    //   }
    // The verb name in the PathSpec forms is always the C++ `get` member:
    // the C++ HttpServer exposes `get(const PathSpec&, ...)` uniformly and
    // carries the verb internally? NO — see the runtime bridge in
    // README.md WIRING: the C++ `get`/`post`/… PathSpec overloads take the
    // HTTP method from the member name, so the dispatch must emit
    // `networkMemberName(receiver, m.property)` there too. Fix the two
    // `.get(` spellings above to the member name before pasting.

// === Settings dispatch hunk (same function, before the verb hunk) ========
    // Paste with the verb hunk:
    //   const std::string& prop = m.property;
    //   if (prop == "set" || prop == "enable" || prop == "disable") {
    //       if (c.arguments.size() != (prop == "set" ? 2 : 1)) return "0.0";
    //       return "(" + expr(*m.object) + ").setRoutingSetting(" +
    //              expr(*c.arguments[0]) + ", " +
    //              (prop == "enable" ? "true" : prop == "disable" ? "false" : expr(*c.arguments[1])) + ")";
    //   }
    //   if (prop == "get" || prop == "enabled" || prop == "disabled") {
    //       std::string key = expr(*c.arguments[0]);
    //       std::string read = "(" + expr(*m.object) + ").getRoutingSetting(" + key + ")";
    //       return prop == "disabled" ? "(!" + read + ")" : read;
    //   }

// === generator.h declarations ==============================================
    //   std::string regexRoutingPathText(const Expr& e);
    //   std::string regexRoutingOptionsCtor(const Expr& arg);
    //   // `needsRegex_` must exist on GeneratorState; if it does not, route
    //   // the flag through `needsNetwork_` + the regex include row instead.

// === usage_includes / module_writer =======================================
// Regex::compile is already reachable: the D48 lowering sets `needsRegex_`
// for literals; the route lowering sets it the same way, so no new include
// row is needed beyond the existing `#include "cp3/core/regex.hpp"`.
