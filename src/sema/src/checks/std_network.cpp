// =============================================================================
// src/sema/src/checks/std_network.cpp - `std/network` surface typing.
//
// OWNERSHIP: the network wave (std/network). Two shapes reach this file:
//   * the namespace corner (`network.fetch(...)`, the std_check.cpp row) and
//   * the documented named imports (`import { fetch, createServer, WebSocket,
//     WebSocketServer } from "std/network"`), which are ambient bindings, so
//     their call and construct sites are typed here (call_check.cpp and
//     expr_check.cpp hook `tryInferNetworkCall` / `inferNetworkConstruct`).
//
// The object shapes are OPEN on purpose. The pinned signature surface is the
// documented member set; everything outside it (`headers` / `params` / `query`
// Maps, the streaming body, the event-loop-free request readers) stays ambient
// instead of reporting, exactly like the rest of the not-yet-typed std surface.
// Marker names are shared with codegen (emit/std/network.cpp), which uses them
// to decide which receivers it lowers.
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

}  // namespace

Type Analyzer::networkObjectType(const std::string& name) {
    std::vector<Field> fields;
    if (name == kFetchResponse) {
        fields.push_back(Field{"ok", Type::boolean(), false});
        fields.push_back(Field{"status", Type::number(), false});
        fields.push_back(Field{"statusText", Type::string(), false});
        fields.push_back(Field{"text", Type::function({}, promiseOf(Type::string())), false});
        fields.push_back(
            Field{"arrayBuffer", Type::function({}, promiseOf(Type::string())), false});
        fields.push_back(Field{"json", Type::function({}, promiseOf(ambient())), false});
    } else if (name == kHttpRequest) {
        fields.push_back(Field{"method", Type::string(), false});
        fields.push_back(Field{"path", Type::string(), false});
        fields.push_back(Field{"text", Type::function({}, promiseOf(Type::string())), false});
        fields.push_back(Field{"json", Type::function({}, promiseOf(ambient())), false});
    } else if (name == kHttpResponse) {
        // `status` / `header` chain; the terminal writers return nothing.
        fields.push_back(Field{"status", Type::function(permissiveParams(1), ambient()), false});
        fields.push_back(Field{"header", Type::function(permissiveParams(2), ambient()), false});
        fields.push_back(Field{"text", callbackOf(2), false});
        fields.push_back(Field{"json", callbackOf(2), false});
        fields.push_back(Field{"send", callbackOf(1), false});
        fields.push_back(Field{"write", callbackOf(1), false});
        fields.push_back(Field{"end", Type::function({}, Type::voidType()), false});
    } else if (name == kHttpServer) {
        const Type route = callbackOf(2);
        for (const char* verb : {"get", "post", "put", "delete_", "patch", "head", "options",
                                 "any"}) {
            fields.push_back(Field{verb, Type::function({Type::string(), route},
                                                        Type::voidType()),
                                   false});
        }
        fields.push_back(Field{"del", Type::function({Type::string(), route}, Type::voidType()),
                               false});
        fields.push_back(Field{"use", Type::function({callbackOf(3)}, Type::voidType()), false});
        fields.push_back(
            Field{"listen", Type::function({Type::number(), callbackOf(0)}, Type::voidType()),
                  false});
        fields.push_back(Field{"close", Type::function({}, Type::voidType()), false});
    } else if (name == kWebSocket) {
        fields.push_back(Field{"on", Type::function({Type::string(), eventListener()},
                                                    Type::voidType()),
                               false});
        fields.push_back(Field{"send", callbackOf(1), false});
        fields.push_back(Field{"close", callbackOf(2), false});
    } else if (name == kWebSocketServer) {
        fields.push_back(Field{"on", Type::function({Type::string(), eventListener()},
                                                    Type::voidType()),
                               false});
        fields.push_back(Field{"close", Type::function({}, Type::voidType()), false});
    } else {
        return Type::unknown();
    }
    Type object = makeObjectType(std::move(fields), true, false, {});
    object.name = name;
    return object;
}

std::optional<Type> Analyzer::networkMember(const std::string& prop) {
    if (prop == "fetch") {
        return Type::function({Type::string(), ambient()},
                              promiseOf(networkObjectType(kFetchResponse)));
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
