// =============================================================================
// sema_cookie.inc - PASTE-IN snippet for src/std/network/codegen/sema.cpp
// (lead owns that file; this subagent must not touch shared files).
// Each block is anchored to the exact existing line it extends.
// =============================================================================

// --- 1. httpRequestFields(): after line 151 `Field{"cookies", stringMap(), false},`
//        Field{"signedCookies", stringMap(), false},

// --- 2. httpResponseFields(): after the existing `Field{"cookie", ...}` row
//        (lines 193-197), add:
//        Field{"clearCookie",
//              withOptionalTail(Type::function(
//                  {Type::string(), Type::unknown()},
//                  networkValue(kHttpResponse))),
//              false},

// --- 3. httpServerFields(): after the `use` row (lines 216-217), add:
//        // webserver `app.set(name, value)` (chainable). Only "secret" is
//        // lowered (onto the cookie secret registry); other keys are no-ops.
//        // `app.get(name)` as a settings getter is NOT added: `get` is the
//        // route verb in this table (see README limitations).
//        fields.push_back(Field{"set",
//              Type::function({Type::string(), Type::unknown()},
//                             networkValue(kHttpServer)),
//              false});

// --- 4. networkObjectMemberDoc():
// (a) HttpRequest, after the `cookies` doc (line 385), add:
//     if (member == "signedCookies") return {"signedCookies: Map<string, string>", "Verified signed cookies (`s:` values that pass HMAC verification against the secret from `app.set(\"secret\", ...)` / `cookieParser(secret)`). Tampered cookies are omitted. Empty when no secret is configured."};
// (b) HttpResponse, replace the `cookie` doc (line 399) with the full surface:
//     if (member == "cookie") return {"cookie(name: string, value: string, options?: CookieOptions): HttpResponse", "Appends a `Set-Cookie` header. options: `maxAge` (milliseconds, webserver semantics), `expires` (Date as epoch-ms number or IMF-fixdate string), `path`, `domain`, `secure`, `httpOnly`, `sameSite` (`\"strict\"`|`\"lax\"`|`\"none\"`), `signed` (requires a secret). Returns the response for chaining."};
//     if (member == "clearCookie") return {"clearCookie(name: string, options?: CookieOptions): HttpResponse", "Appends an expired `Set-Cookie` for `name` (Expires in the past, Max-Age=0, Path=/ by default). Returns the response for chaining."};
// (c) HttpServer, after the `use` doc (line 372), add:
//     if (member == "set") return {"set(name: string, value: unknown): HttpServer", "webserver-style settings setter, chainable. Only `set(\"secret\", ...)` is lowered (the cookie-signing secret); other keys are accepted and ignored."};

// --- 5. Analyzer::networkMember(): after the `static/cors/bodyLimit` row
//        (lines 479-481), add:
//     if (prop == "cookieParser") {
//         return withOptionalTail(Type::function({ambient()}, httpMiddleware()));
//     }

// --- 6. Analyzer::tryInferNetworkCall(): next to the static/cors/bodyLimit
//        arm (lines 520-524), add:
//     if (id.name == "cookieParser") {
//         inferArgs(call);
//         if (call.arguments.size() > 1) {
//             report(diagcode::kArityMismatch, anchorSpan(call.span),
//                    "cookieParser(secret?) takes at most one argument");
//         }
//         out = httpMiddleware();
//         return true;
//     }
