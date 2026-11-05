// =============================================================================
// src/std/network/express/router/codegen_router.cpp - `Router` lowering.
//
// OWNERSHIP: the std/network family
// (src/std/network/express/router/**). SPLICE FRAGMENT: this file is code
// to merge into src/std/network/codegen/codegen.cpp (construction +
// receiver recognition) and src/std/network/codegen/codegen_methods.cpp
// (member calls/reads), at the marked points. It is NOT compiled on its
// own (the codegen TU list in src/codegen/CMakeLists.txt is explicit, so
// this file is inert until spliced). Runtime namespace:
// ::cp3::runtime::network::express:: (cp3/std/network/express/router.hpp).
//
// The Router reuses the network scaffolding: handler arrows are emitted
// with declared parameter types (networkArg), `delete` maps to `del`,
// async handlers lower to coroutine lambdas, and the Map-shaped fields
// keep the collections delegation. Only the Router-specific arms below
// are new.
//
// WIRING (lead applies):
//   codegen.cpp: [C1] consts, [C2] isNetworkObject arm, [C8] Router
//     construction (+ options), [C9] the `app.use(..., router)` special
//     case + [C6a] networkArgs nothing (member args are plain).
//   codegen_methods.cpp: [C3] networkFieldType arms, [C4] member-read
//     arms, [C5] member-name arm, [C6] handler-typing contexts +
//     async-lambda widening, [C7] param-handler param type.
//   Shared files (lead owns them):
//     src/codegen/src/emit/decl/program.cpp (line ~206) and
//     src/codegen/src/support/type/module_writer.cpp (line ~152):
//       if (n.network)  out += "#include \"cp3/std/network/express/router.hpp\"\n";
//       (module_writer: if (needsNetwork_) ...) - piggybacks on the
//       network include because every Router program imports std/network;
//       the native row's include dir (native.hpp) puts the header on the
//       compile path.
// =============================================================================

namespace cp3 {
using namespace detail;
namespace {

// [C1] Marker consts (after kAbortSignal, in codegen.cpp and
// codegen_methods.cpp anonymous namespaces):
constexpr const char* kRouter = "$network:Router";   // Ledger R (Router)
constexpr const char* kRoute = "$network:Route";     // Ledger R (Router)

// [C2] In Generator::isNetworkObject (codegen.cpp), extend the name list:
        name == kRouter || name == kRoute ||
// (then the existing chainable status/header tail is unchanged: Router
// has no chainable members; `router.route(...)` returns kRoute from
// sema, which is how the Route arms below are recognised.)

}  // namespace

// [C8] In Generator::emitNetworkNew (codegen.cpp), at the top:
// Ledger R (Router, appended): `new Router()` / `new Router(options?)`.
    if (name == "Router") {
        needsNetwork_ = true;
        std::string options = std::string(kRuntimeNs) + "express::RouterOptions{}";
        if (!n.arguments.empty() && n.arguments.front() &&
            n.arguments.front()->kind == NodeKind::ObjectLiteral) {
            const auto& lit = static_cast<const ObjectLiteral&>(*n.arguments.front());
            std::string inner;
            for (const auto& p : lit.properties) {
                if (!p || !p->value) continue;
                const std::string key = objectKey(*p);
                // RouterOptions{} fields: caseSensitive / strict / mergeParams.
                if (key == "caseSensitive") inner += ".caseSensitive = " + expr(*p->value) + "; ";
                else if (key == "strict") inner += ".strict = " + expr(*p->value) + "; ";
                else if (key == "mergeParams") inner += ".mergeParams = " + expr(*p->value) + "; ";
            }
            options = "[&] { " + std::string(kRuntimeNs) + "express::RouterOptions o; " + inner + "return o; }()";
        } else if (!n.arguments.empty() && n.arguments.front()) {
            reportTypeError(*n.arguments.front(), "type mismatch: new Router() options must be an object literal");
        }
        return std::string(kRuntimeNs) + "express::Router(" + options + ")";
    }

// [C9] In Generator::emitNetworkMemberCall (codegen_methods.cpp), at the
// top (right after `const Type receiver = ...;`):
    // Ledger R (Router, appended): `app.use("/api", router)` and
    // `app.use(router)` lower to the express mount middleware. The
    // HttpServer has no mounting member of its own, so the check is on
    // the *argument*'s type: a kRouter argument means mounting.
    if (receiver.name == kHttpServer && m.property == "use") {
        const std::string pathArg =
            (c.arguments.size() >= 2 && c.arguments.front())
                ? expr(*c.arguments.front()) : std::string();
        const Expr* routerExpr = nullptr;
        if (c.arguments.size() >= 2 && c.arguments[1] &&
            effectiveType(*c.arguments[1]).name == kRouter) {
            routerExpr = c.arguments[1].get();
        } else if (c.arguments.size() == 1 && c.arguments.front() &&
                   effectiveType(*c.arguments.front()).name == kRouter) {
            routerExpr = c.arguments.front().get();
        }
        if (routerExpr != nullptr) {
            needsNetwork_ = true;
            const std::string routerText = expr(*routerExpr);
            // (codegen_methods.cpp has no kRuntimeNs constant; the arms
            // below spell the runtime namespace literally.)
            if (pathArg.empty()) {
                return "(" + expr(*m.object) + ").use((" + routerText +
                       ").handler())";
            }
            return "(" + expr(*m.object) + ").use((" + routerText +
                   ").mountMiddleware(" + pathArg + "))";
        }
    }

// ---------------------------------------------------------------------------
// codegen_methods.cpp arms (same namespace block as the above; Generator
// member functions):
// ---------------------------------------------------------------------------

// [C5] In networkMemberName (codegen_methods.cpp):
    if ((receiver.name == kRouter || receiver.name == kRoute) &&
        property == "delete") return "del";

// [C3] In Generator::networkFieldType (codegen_methods.cpp), inside the
// kHttpRequest arm:
        if (m.property == "baseUrl" || m.property == "originalUrl") {
            return Type::string();
        }

// [C4a] In Generator::emitNetworkMemberRead (codegen_methods.cpp), at
// the top (after `const std::string& name = ...;`): the two reads do not
// exist on HttpRequest (the side table owns them), so they lower to the
// express accessors. needsNetwork_ is set by the prefix include wiring.
    if (name == kHttpRequest && (m.property == "baseUrl" || m.property == "originalUrl")) {
        needsNetwork_ = true;
        const std::string fn = m.property == "baseUrl" ? "requestBaseUrl" : "requestOriginalUrl";
        return std::string("::cp3::runtime::network::express::") + fn + "(" + expr(*m.object) + ")";
    }
// (and extend the kHttpRequest `known` list with the same two names for
// the generic fall-through; the special case above already returns, so
// no generic change is required.)

// [C6] In Generator::networkArg (codegen_methods.cpp), extend the
// context block and the async-lambda condition:
    // (inside `if (receiver.name == kHttpServer) {...}` the existing
    // arms cover `use`/`onError`/verbs; add after it:)
    else if (receiver.name == kRouter) {
        if (property == "param") {
            // (req, res, next, id): contextType needs the string arm [C7].
            context = {kReqType, kResType, kNextType, "const std::string&"};
        } else if (property == "use") {
            context = {kReqType, kResType, kNextType};
        } else {
            context = {kReqType, kResType};
        }
    } else if (receiver.name == kRoute) {
        // `router.route(path).get(handler)`: (req, res).
        context = {kReqType, kResType};
    }
    // Async coroutine lambdas: widen the condition from
    // `receiver.name == kHttpServer` to also cover kRouter / kRoute,
    // so `async` handlers on a router can `await req.text()`. The
    // runtime side (makeRoute/makeParamHandler) already waits on the
    // returned promise.

// [C7] In contextType (codegen_methods.cpp), add the string arm so a
// `router.param("id", (req, res, next, id) => ...)` handler's fourth
// parameter is declared as a string in the temporary handler scope:
    if (cpp == "const std::string&" || cpp == "std::string") return Type::string();
// (the existing arm matches "std::string"; extend it with the const&
// spelling used by [C6].)

}  // namespace cp3
