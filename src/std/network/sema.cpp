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
// of the not-yet-typed std surface. The Field rows below are declarative then:
// they are the documented member set (docs 04 §network), kept complete so that
// closing these shapes later is a table-only change, but they do not gate
// member usage today.
// Marker names are shared with codegen (src/std/network/codegen.cpp), which
// uses them to decide which receivers it lowers.
// =============================================================================
#include "detail/analyzer.h"

#include <optional>
#include <string>
#include <vector>

namespace cp3 {

namespace {

const char* const kFetchResponse = "$network:FetchResponse";
const char* const kHttpRequest = "$network:HttpRequest";
const char* const kHttpResponse = "$network:HttpResponse";
const char* const kHttpServer = "$network:HttpServer";
const char* const kWebSocket = "$network:WebSocket";
const char* const kWebSocketServer = "$network:WebSocketServer";

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
// The documented field rows per receiver (docs 04 §network). Declarative: the
// marker shapes stay open, so these rows never gate a member (file header);
// SEAM comments mark the in-flight-family swap points.
// -----------------------------------------------------------------------------
std::vector<Field> fetchResponseFields() {
    return {
        Field{"ok", Type::boolean(), false},
        Field{"status", Type::number(), false},
        Field{"statusText", Type::string(), false},
        // SEAM(collections): `headers: Map<string, string>`; the runtime
        // already buffers the header block (runtime_fetch.cpp parseHead).
        Field{"headers", ambient(), false},
        Field{"text", Type::function({}, promiseOf(Type::string())), false},
        // SEAM(json): non-scalar T needs the json reification (see codegen's
        // networkTypeArgsText); the ambient return stands in until then.
        Field{"json", Type::function({}, promiseOf(ambient())), false},
        // SEAM(buffer): the documented return is `Promise<Buffer>`; the
        // runtime hands the buffered bytes back as a string until Buffer lands.
        Field{"arrayBuffer", Type::function({}, promiseOf(Type::string())), false},
    };
}

std::vector<Field> httpRequestFields() {
    return {
        Field{"method", Type::string(), false},
        Field{"path", Type::string(), false},
        // SEAM(collections): the three Map<string, string> fields; the server
        // already receives the query string, the route params and the header
        // block natively (runtime_server.cpp onRequest).
        Field{"params", ambient(), false},
        Field{"query", ambient(), false},
        Field{"headers", ambient(), false},
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
        // SEAM(buffer): the documented parameter is `string | Buffer`.
        Field{"send", callbackOf(1), false},
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

}  // namespace

Type Analyzer::networkObjectType(const std::string& name) {
    std::vector<Field> fields;
    if (name == kFetchResponse) {
        fields = fetchResponseFields();
    } else if (name == kHttpRequest) {
        fields = httpRequestFields();
    } else if (name == kHttpResponse) {
        fields = httpResponseFields();
    } else if (name == kHttpServer) {
        fields = httpServerFields();
    } else if (name == kWebSocket) {
        fields = webSocketFields();
    } else if (name == kWebSocketServer) {
        fields = webSocketServerFields();
    } else {
        return Type::unknown();
    }
    // The open flag drops the shape: what sema keeps is the marker name (see
    // the file header). The rows above are the audit surface for a later round.
    Type object = makeObjectType(std::move(fields), true, false, {});
    object.name = name;
    return object;
}

std::optional<Type> Analyzer::networkMember(const std::string& prop) {
    if (prop == "fetch") {
        // `network.fetch(url, init?)`: the init is ambient until RequestInit
        // has a shape; the trailing optional keeps the documented one-argument
        // call from reporting an arity mismatch.
        return withOptionalTail(Type::function(
            {Type::string(), ambient()}, promiseOf(networkObjectType(kFetchResponse))));
    }
    if (prop == "createServer") return Type::function({}, networkObjectType(kHttpServer));
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
        out = promiseOf(networkObjectType(kFetchResponse));
        return true;
    }
    if (id.name == "createServer") {
        inferArgs(call);
        out = networkObjectType(kHttpServer);
        return true;
    }
    return false;
}

Type Analyzer::inferNetworkConstruct(const std::string& name, ExprList& args,
                                     const SourceSpan& span) {
    if (name != "WebSocket" && name != "WebSocketServer") return Type::unknown();
    (void)span;
    for (auto& arg : args) {
        if (arg) inferExpr(*arg);
    }
    return name == "WebSocket" ? networkObjectType(kWebSocket)
                               : networkObjectType(kWebSocketServer);
}

}  // namespace cp3
