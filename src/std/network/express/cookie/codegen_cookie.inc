// =============================================================================
// codegen_cookie.inc - PASTE-IN snippet for src/std/network/codegen/
// codegen_methods.cpp (+ codegen.cpp for cookieParser). Lead owns those files.
// Uses only members already on Generator: expr(), effectiveType(),
// reportCodegenError(), needsNetwork_/needsEventLoop_.
// Assumes generated code includes "cp3/std/network/express_cookie.hpp"
// (see README wiring) so `::cp3::runtime::network::express::` resolves.
// =============================================================================

// -----------------------------------------------------------------------------
// A. In Generator::emitNetworkMemberCall, REPLACE the existing `cookie` arm
//    (lines 383-427) with the block below, and add the clearCookie/set arms
//    after it. `CookieOptions` is built in generated C++ from the object
//    literal; only literal option values are read (same rule as before).
// -----------------------------------------------------------------------------
/*
    if ((m.property == "cookie" || m.property == "clearCookie") &&
        effectiveType(*m.object).name == kHttpResponse) {
        // res.cookie(name, value, options?) / res.clearCookie(name, options?)
        if (c.arguments.size() < (m.property == "cookie" ? 2u : 1u) ||
            !c.arguments[0]) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               "arity mismatch: res." + m.property +
                                   "() expects a name (and a value for cookie)");
            return "0.0";
        }
        // Build `::cp3::runtime::network::express::CookieOptions __cookieOpts;`
        // field-by-field. Non-literal options / non-literal field values are a
        // codegen error (the header value must be built at codegen time).
        std::string opts = "([&]{ ::cp3::runtime::network::express::CookieOptions o;";
        if (c.arguments.size() >= (m.property == "cookie" ? 3u : 2u) &&
            c.arguments[m.property == "cookie" ? 2 : 1]) {
            const Expr& optArg = *c.arguments[m.property == "cookie" ? 2 : 1];
            if (optArg.kind != NodeKind::ObjectLiteral) {
                reportCodegenError(c, diagcode::kTypeMismatch,
                                   "res." + m.property +
                                       "() options must be an object literal");
                return "0.0";
            }
            for (const auto& propertyPtr :
                 static_cast<const ObjectLiteral&>(optArg).properties) {
                const ObjectProperty* p = propertyPtr.get();
                if (!p || !p->value || p->isMethod || !p->key ||
                    p->key->kind != NodeKind::Identifier) continue;
                const std::string field = static_cast<Identifier&>(*p->key).name;
                const Expr& v = *p->value;
                if (field == "maxAge") {
                    // Express `maxAge` is MILLISECONDS; the header stores
                    // seconds. (Drop `/1000.0` to keep CP3-seconds compat.)
                    opts += " o.hasMaxAge=true; o.maxAge=(" + expr(v) + ")/1000.0;";
                } else if (field == "expires") {
                    if (v.kind == NodeKind::NumberLiteral)
                        opts += " o.expires=::cp3::runtime::network::express::httpDateFromEpochMs(" + expr(v) + ");";
                    else
                        opts += " o.expires=::cp3::runtime::toString(" + expr(v) + ");";
                } else if (field == "path") {
                    opts += " o.path=::cp3::runtime::toString(" + expr(v) + ");";
                } else if (field == "domain") {
                    opts += " o.domain=::cp3::runtime::toString(" + expr(v) + ");";
                } else if (field == "secure") {
                    opts += " o.secure=(" + expr(v) + ")!=0;";
                } else if (field == "httpOnly") {
                    opts += " o.httpOnly=(" + expr(v) + ")!=0;";
                } else if (field == "signed") {
                    opts += " o.signed_= (" + expr(v) + ")!=0;";
                } else if (field == "sameSite") {
                    if (v.kind == NodeKind::StringLiteral) {
                        const std::string s = static_cast<StringLiteral&>(v).value;
                        if (s == "strict") opts += " o.sameSite=::cp3::runtime::network::express::SameSite::kStrict;";
                        else if (s == "lax") opts += " o.sameSite=::cp3::runtime::network::express::SameSite::kLax;";
                        else if (s == "none") opts += " o.sameSite=::cp3::runtime::network::express::SameSite::kNone;";
                        else opts += " o.sameSite=::cp3::runtime::network::express::SameSite::kUnset;";
                    } else {
                        reportCodegenError(c, diagcode::kTypeMismatch,
                                           "res.cookie() 'sameSite' must be a string literal: \"strict\"|\"lax\"|\"none\"");
                        return "0.0";
                    }
                }
                // Unknown option keys are ignored, like JS.
            }
        }
        opts += " return o; }())";
        if (m.property == "cookie") {
            return "::cp3::runtime::network::express::setCookie((" + expr(*m.object) +
                   "), " + expr(*c.arguments[0]) + ", " + expr(*c.arguments[1]) +
                   ", " + opts + ")";
        }
        return "::cp3::runtime::network::express::clearCookie((" + expr(*m.object) +
               "), " + expr(*c.arguments[0]) + ", " + opts + ")";
    }
    if (m.property == "set" && effectiveType(*m.object).name == kHttpServer) {
        // app.set("secret", s): register the cookie-signing secret, chainable.
        if (c.arguments.size() == 2 && c.arguments[0] && c.arguments[1] &&
            c.arguments[0]->kind == NodeKind::StringLiteral &&
            static_cast<StringLiteral&>(*c.arguments[0]).value == "secret") {
            return "(::cp3::runtime::network::express::setCookieSecret(" +
                   expr(*c.arguments[1]) + "), (" + expr(*m.object) + "))";
        }
        return "(" + expr(*m.object) + ")"; // other keys: accepted, no-op (yet)
    }
*/

// -----------------------------------------------------------------------------
// B. `req.signedCookies` read.
//     1. Generator::networkFieldType (codegen_methods.cpp ~line 207): add
//        "signedCookies" to the HttpRequest Map-shaped list:
//            if (m.property == "params" || ... || m.property == "cookies" ||
//                m.property == "signedCookies") return stringMapType();
//     2. Generator::emitNetworkMemberRead: before the Map-field delegation,
//        intercept (signedCookies is computed, not a stored field):
//            if (recv == kHttpRequest && m.property == "signedCookies")
//                return "::cp3::runtime::network::express::signedCookiesFor(" +
//                       expr(*m.object) + ")";
// -----------------------------------------------------------------------------
// C. `cookieParser(secret?)` bare call + member-free read.
//     In codegen.cpp's emitNetworkIdentifier (next to static/cors/bodyLimit):
//            if (name == "cookieParser") {
//                if (args.empty()) return kRuntimeNs-Express +
//                    "makeCookieParserMiddleware()";
//                // string | string[] in Express; array lowering is lead's
//                // collections array text - string form is the pinned path.
//                return "::cp3::runtime::network::express::makeCookieParserMiddleware(" +
//                       emit(*args.front()) + ")";
//            }
//     And in sema/emit, `app.use(cookieParser("secret"))` then flows through
//     the existing middleware path unchanged.
// -----------------------------------------------------------------------------
