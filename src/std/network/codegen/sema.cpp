// =============================================================================
// src/std/network/sema.cpp - `std/network` surface typing.
//
// OWNERSHIP: the std/network family (src/std/network/**). Two shapes reach this file:
//   * the namespace corner (`network.fetch(...)`, the std_check.cpp row) and
//   * the documented named imports (`import { fetch, createServer, WebSocket,
//     WebSocketServer } from "std/network"`), which are ambient bindings, so
//     their call and construct sites are typed here (call_check.cpp and
//     expr_check.cpp hook `tryInferNetworkCall` / `inferNetworkConstruct`).
//
// The marker objects are OPEN on purpose (round-2 network report, decision
// 10): they carry a `$network:` name and no shape table, so a member outside
// the documented set stays ambient instead of reporting, exactly like the rest
// of the not-yet-typed std surface. The Field rows below are the documented
// member set (docs 04 §network); since the finish phase they also TYPE the
// documented reads: `networkObjectMember` (member_check.cpp hook) returns the
// row type for one of them while an undocumented member still falls through
// to ambient. The Map-shaped rows name the landed `Map<string, string>`
// (std/collections) and `arrayBuffer` promises the landed `Buffer`.
// Marker names are shared with codegen (src/std/network/codegen.cpp), which
// uses them to decide which receivers it lowers.
// =============================================================================
#include "detail/analyzer.h"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "cp3/diag/diagnostic_codes.hpp"

namespace cp3 {

namespace {

/// A numeric `threads` literal, including a negated one (`-1` parses as a
/// unary expression, not a NumberLiteral).
bool literalThreadCount(const Expr& e, double& out) {
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

const char* const kFetchResponse = "$network:FetchResponse";
const char* const kHttpRequest = "$network:HttpRequest";
const char* const kHttpResponse = "$network:HttpResponse";
const char* const kHttpServer = "$network:HttpServer";
const char* const kWebSocket = "$network:WebSocket";
const char* const kWebSocketServer = "$network:WebSocketServer";

/// `Map<string, string>` (std/collections): the documented shape of the
/// request/fetch headers, route params and query string.
Type stringMap() {
    Type map(TypeKind::Class, "Map");
    map.parts.push_back(Type::string());
    map.parts.push_back(Type::string());
    return map;
}

/// The nominal std/buffer `Buffer` (src/std/buffer/sema.cpp).
Type bufferType() { return byteBufferType(); }

/// `(p0, p1, ...) => void` with permissive parameters: the documented handler
/// shapes share one callback grammar, and the parameters are typed contextually
/// by codegen (the runtime lambda's declared parameter types).
std::vector<Type> permissiveParams(std::size_t arity) {
    return std::vector<Type>(arity, ambient());
}

Type callbackOf(std::size_t arity) {
    return Type::function(permissiveParams(arity), Type::voidType());
}

Type eventListener() { return callbackOf(2); }

// -----------------------------------------------------------------------------
// The documented field rows per receiver (docs 04 §network). The marker shapes
// stay open, so the rows never gate a member (file header); the
// `networkObjectMember` hook hands a documented read its row type. SEAM
// comments mark the still-blocked swap points (the fs ReadStream body, the
// headers ambiguity).
// -----------------------------------------------------------------------------
std::vector<Field> fetchResponseFields() {
    return {
        Field{"ok", Type::boolean(), false},
        Field{"status", Type::number(), false},
        Field{"statusText", Type::string(), false},
        // Map<string, string>; the runtime fills it in parseHead
        // (runtime_fetch.cpp) from the response header block.
        Field{"headers", stringMap(), false},
        Field{"text", Type::function({}, promiseOf(Type::string())), false},
        // SEAM(json): non-scalar T is routed through the json reification at
        // codegen (networkTypeArgsText / the record arm); the
        // `tryInferNetworkMemberCall` hook substitutes the explicit T here.
        Field{"json", Type::function({}, promiseOf(ambient())), false},
        // The landed std/buffer `Buffer`.
        Field{"arrayBuffer", Type::function({}, promiseOf(bufferType())), false},
    };
}

std::vector<Field> httpRequestFields() {
    return {
        Field{"method", Type::string(), false},
        Field{"path", Type::string(), false},
        // The three Map<string, string> fields; the server fills them in
        // onRequest (runtime_server.cpp) from the native query string, the
        // header block and the matched `:name` route params.
        Field{"params", stringMap(), false},
        Field{"query", stringMap(), false},
        Field{"headers", stringMap(), false},
        // SEAM(readstream/fs): `body: ReadStream`; the runtime buffers the
        // body before the chain runs, so nothing stream-shaped exists yet.
        Field{"body", ambient(), false},
        Field{"text", Type::function({}, promiseOf(Type::string())), false},
        Field{"json", Type::function({}, promiseOf(ambient())), false},
    };
}

std::vector<Field> httpResponseFields() {
    return {
        // `status` / `header` chain; the terminal writers return nothing.
        Field{"status", Type::function(permissiveParams(1), ambient()), false},
        Field{"header", Type::function(permissiveParams(2), ambient()), false},
        // SEAM(json): an object/array payload needs json's serializer; the
        // runtime overloads take a scalar today (cp3/std/network.hpp).
        Field{"text", withOptionalTail(callbackOf(2)), false},
        Field{"json", withOptionalTail(callbackOf(2)), false},
        Field{"send", withOptionalTail(callbackOf(1)), false},
        Field{"write", callbackOf(1), false},
        Field{"end", Type::function({}, Type::voidType()), false},
    };
}

std::vector<Field> httpServerFields() {
    std::vector<Field> fields;
    const Type route = callbackOf(2);
    // The documented four verbs plus the Express-style extras the runtime grew
    // so this table stays truthful; codegen maps the cp3 `delete` onto the
    // runtime `del` member (`delete` is a C++ keyword).
    for (const char* verb : {"get", "post", "put", "delete", "patch", "head", "options",
                             "any"}) {
        fields.push_back(Field{verb, Type::function({Type::string(), route}, Type::voidType()),
                               false});
    }
    fields.push_back(Field{"use", Type::function({callbackOf(3)}, Type::voidType()), false});
    fields.push_back(
        Field{"listen",
              withOptionalTail(
                  Type::function({Type::number(), callbackOf(0)}, Type::voidType())),
              false});
    fields.push_back(Field{"close", Type::function({}, Type::voidType()), false});
    return fields;
}

std::vector<Field> webSocketFields() {
    return {
        Field{"on", Type::function({Type::string(), eventListener()}, Type::voidType()),
              false},
        // `send(data: string | Buffer)` (docs 04 §network): the landed
        // std/buffer type joins the string arm, exactly like the crypto
        // hash inputs (src/std/crypto/sema.cpp).
        Field{"send",
              Type::function({Type::unionOf({Type::string(), bufferType()})},
                             Type::voidType()),
              false},
        Field{"close", withOptionalTail2(callbackOf(2)), false},
    };
}

std::vector<Field> webSocketServerFields() {
    return {
        Field{"on", Type::function({Type::string(), eventListener()}, Type::voidType()),
              false},
        Field{"close", Type::function({}, Type::voidType()), false},
    };
}

/// The documented field rows of one marker table, or {} when `name` is not a
/// network marker. Shared by `networkObjectType` (the marker itself) and
/// `networkObjectMember` (one documented read).
std::vector<Field> networkFieldsFor(const std::string& name) {
    if (name == kFetchResponse) return fetchResponseFields();
    if (name == kHttpRequest) return httpRequestFields();
    if (name == kHttpResponse) return httpResponseFields();
    if (name == kHttpServer) return httpServerFields();
    if (name == kWebSocket) return webSocketFields();
    if (name == kWebSocketServer) return webSocketServerFields();
    return {};
}

}  // namespace

Type Analyzer::networkObjectType(const std::string& name) {
    std::vector<Field> fields = networkFieldsFor(name);
    if (fields.empty()) return Type::unknown();
    // The open flag drops the shape: what sema keeps is the marker name (see
    // the file header). `networkObjectMember` below is what makes a documented
    // read carry its declared type anyway.
    Type object = makeObjectType(std::move(fields), true, false, {});
    object.name = name;
    return object;
}

std::optional<Type> Analyzer::networkObjectMember(const Type& recv,
                                                  const std::string& prop) {
    if (recv.kind != TypeKind::Object) return std::nullopt;
    const std::vector<Field> fields = networkFieldsFor(recv.name);
    for (const Field& field : fields) {
        if (field.name == prop) return field.type;
    }
    return std::nullopt;
}

std::optional<Type> Analyzer::networkMember(const std::string& prop) {
    if (prop == "fetch") {
        // `network.fetch(url, init?)`: the init is ambient until RequestInit
        // has a shape; the trailing optional keeps the documented one-argument
        // call from reporting an arity mismatch.
        return withOptionalTail(Type::function(
            {Type::string(), ambient()}, promiseOf(networkObjectType(kFetchResponse))));
    }
    if (prop == "createServer") {
        // `createServer(options?)` (D79): one optional objects argument.
        return withOptionalTail(
            Type::function({ambient()}, networkObjectType(kHttpServer)));
    }
    if (prop == "WebSocket") {
        return Type::function({Type::string()}, networkObjectType(kWebSocket));
    }
    if (prop == "WebSocketServer") {
        return Type::function({ambient()}, networkObjectType(kWebSocketServer));
    }
    return std::nullopt;
}

bool Analyzer::tryInferNetworkCall(CallExpr& call, const Identifier& id, Type& out) {
    if (id.name == "fetch") {
        inferArgs(call);
        // `fetch(url, options?)` (docs 04 §network): the URL is required, the
        // options object is optional. A call without the URL is a program
        // error here, not a codegen skip: the runtime has no URL to fetch.
        if (call.arguments.size() > 2 || call.arguments.empty()) {
            report(diagcode::kArityMismatch, anchorSpan(call.span),
                   "fetch(url, options?) takes one or two arguments");
        }
        out = promiseOf(networkObjectType(kFetchResponse));
        return true;
    }
    if (id.name == "createServer") {
        if (checkCreateServerOptions(call)) out_.networkScaleMode = true;
        out = networkObjectType(kHttpServer);
        return true;
    }
    return false;
}

/// D79: `createServer(options?)`. The only accepted shape is one object
/// literal with an optional `threads` property. Literal thread counts are
/// checked here (0, negative, fractional and non-number literals become
/// CP3401); a computed value is validated by the runtime constructor, and the
/// literal 1 keeps the single-loop path.
bool Analyzer::checkCreateServerOptions(CallExpr& call) {
    inferArgs(call);
    if (call.arguments.empty()) return false;
    if (call.arguments.size() > 1 || !call.arguments.front()) {
        report(diagcode::kTypeMismatch, anchorSpan(call.span),
               "createServer() takes at most one argument (an options object: "
               "{ threads?: number })");
        return false;
    }
    const Expr& options = *call.arguments.front();
    if (options.kind != NodeKind::ObjectLiteral) {
        report(diagcode::kTypeMismatch, anchorSpan(options.span),
               "createServer() options must be an object literal: "
               "{ threads?: number }");
        return false;
    }
    bool scale = false;
    const auto& literal = static_cast<const ObjectLiteral&>(options);
    for (const auto& propPtr : literal.properties) {
        const ObjectProperty* prop = propPtr.get();
        if (prop == nullptr) continue;
        if (prop->computed || prop->key == nullptr) {
            report(diagcode::kTypeMismatch, anchorSpan(prop->span),
                   "createServer() options only support the 'threads' property");
            continue;
        }
        const std::string key = keyName(prop->key.get());
        if (key != "threads") {
            report(diagcode::kTypeMismatch, anchorSpan(prop->span),
                   "unknown createServer() option '" + key +
                       "' (only 'threads' is supported)");
            continue;
        }
        if (prop->value == nullptr) continue;
        double literalCount = 0.0;
        if (literalThreadCount(*prop->value, literalCount)) {
            if (!std::isfinite(literalCount) || literalCount < 1.0 ||
                literalCount != std::floor(literalCount)) {
                report(diagcode::kTypeMismatch, anchorSpan(prop->value->span),
                       "createServer() 'threads' must be an integer >= 1");
            } else if (literalCount != 1.0) {
                scale = true;
            }
            continue;
        }
        // A computed value may still be known to be non-numeric; otherwise it
        // is accepted and checked by the runtime constructor.
        const Type valueType = typeOfExpr(*prop->value);
        const bool numericLiteral =
            valueType.kind == TypeKind::Literal &&
            valueType.literalBaseType().kind == TypeKind::Number;
        if (valueType.kind == TypeKind::Number || numericLiteral ||
            valueType.kind == TypeKind::Unknown ||
            valueType.kind == TypeKind::Error) {
            scale = true;
        } else {
            report(diagcode::kTypeMismatch, anchorSpan(prop->value->span),
                   "createServer() 'threads' must be a number");
        }
    }
    return scale;
}

Type Analyzer::inferNetworkConstruct(const std::string& name, ExprList& args,
                                     const SourceSpan& span) {
    if (name != "WebSocket" && name != "WebSocketServer") return Type::unknown();
    for (auto& arg : args) {
        if (arg) inferExpr(*arg);
    }
    // `new WebSocket(url)` and `new WebSocketServer({ port, path? })`
    // (docs 04 §network): both take exactly one argument. A call without it
    // is a program error here (the runtime has no URL / no port to bind), not
    // a codegen skip.
    if (args.size() != 1) {
        report(diagcode::kArityMismatch, anchorSpan(span),
               name == "WebSocket" ? "new WebSocket(url) takes one argument"
                                   : "new WebSocketServer(options) takes one "
                                     "argument");
    }
    return name == "WebSocket" ? networkObjectType(kWebSocket)
                               : networkObjectType(kWebSocketServer);
}

bool Analyzer::tryInferNetworkMemberCall(CallExpr& call, MemberExpr& m, Type& out) {
    // `network.createServer(...)` (D79): the namespace spelling of the same
    // construction path, with the same options validation.
    if (m.property == "createServer" && m.object &&
        m.object->kind == NodeKind::Identifier) {
        const auto& id = static_cast<const Identifier&>(*m.object);
        Binding binding;
        if (id.name == "network" && lookup(id.name, binding) && binding.isAmbient) {
            if (checkCreateServerOptions(call)) out_.networkScaleMode = true;
            out = networkObjectType(kHttpServer);
            return true;
        }
    }
    if (m.property != "json" || m.object == nullptr) return false;
    const Type recv = typeOfExpr(*m.object);
    if (recv.kind != TypeKind::Object) return false;
    if (recv.name != kFetchResponse && recv.name != kHttpRequest) return false;
    if (call.typeArgs.empty() || !call.typeArgs.front()) return false;
    // `res.json<T>()` / `req.json<T>()` takes no arguments (docs 04 §network);
    // the explicit T substitutes into the promise exactly like
    // `JSON.parse<T>` (call_check.cpp's tryInferJsonCall), so `await
    // res.json<User>()` is the interface shape and `u.id` lowers through the
    // json record tables.
    Type value = Type::unknown();
    for (auto& ta : call.typeArgs) {
        if (!ta) continue;
        value = resolveType(*ta);
    }
    FuncSig sig;
    sig.annotatedReturn = true;
    sig.name = "json";
    sig.ret = promiseOf(value);
    checkArgs(sig, call);
    out = withType(call, sig.ret);
    return true;
}

}  // namespace cp3
