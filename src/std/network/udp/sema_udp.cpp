// =============================================================================
// PASTE SNIPPETS for src/std/network/sema.cpp (UDP sub-surface).
//
// OWNERSHIP: the std/network family (src/std/network/udp/**). This file is
// NOT compiled directly; the lead pastes the blocks below into sema.cpp.
// Do not include this file from any TU.
//
// cp3 spelling:
//   import { createUdpSocket } from "std/network";
//   const socket = createUdpSocket("udp4");
//   socket.bind(4000);
//   socket.on("message", (msg, from) => { console.log(from.address, from.port, from.family); });
//   socket.send(msg, 4001, "127.0.0.1");
//   socket.close();
// =============================================================================

// ---- 1. Marker constants (next to kWebSocket / kWebSocketServer) ----------
const char* const kUdpSocket = "$network:UdpSocket";
const char* const kUdpRemoteInfo = "$network:UdpRemoteInfo";

// ---- 2. Field rows (next to webSocketFields / webSocketServerFields) -------
std::vector<Field> udpSocketFields(TypeSystem ts) {
    // `from` in the message handler: { address: string, port: Port, family: string }
    // Typed permissively here; codegen lowers the handler parameters to
    // (const buffer::Buffer&, UdpRemoteInfo) and member reads off the struct.
    Type messageHandler = Type::function(
        {bufferType(), networkValue(kUdpRemoteInfo)}, Type::voidType());
    return {
        Field{"on",
              Type::function({Type::string(), Type::function({ambient(), ambient()}, Type::voidType())},
                             Type::voidType()),
              false},
        // `send(data: Buffer | string, port: Port, address: string)`
        Field{"send",
              Type::function({Type::unionOf({bufferType(), Type::string()}),
                              stdIntegerType(StdIntegerKind::Port, ts), Type::string()},
                             Type::voidType()),
              false},
        // `bind(port: Port) | bind(port: Port, address: string)`
        Field{"bind",
              Type::function({stdIntegerType(StdIntegerKind::Port, ts), Type::string()},
                             Type::voidType()),
              false},
        Field{"bind",
              Type::function({stdIntegerType(StdIntegerKind::Port, ts)}, Type::voidType()),
              false},
        Field{"close", Type::function({}, Type::voidType()), false},
        Field{"bound", Type::function({}, Type::boolean()), false},
        Field{"address", Type::function({}, networkValue(kUdpRemoteInfo)), false},
    };
}

std::vector<Field> udpRemoteInfoFields(TypeSystem ts) {
    return {
        Field{"address", Type::string(), false},
        Field{"port", stdIntegerType(StdIntegerKind::Port, ts), false},
        Field{"family", Type::string(), false},
    };
}

// ---- 3. networkFieldsFor dispatch ------------------------------------------
    if (name == kUdpSocket) return udpSocketFields(ts);
    if (name == kUdpRemoteInfo) return udpRemoteInfoFields(ts);

// ---- 4. networkMember (namespace corner) -----------------------------------
    if (prop == "createUdpSocket") {
        return Type::function({Type::string()}, networkObjectType(kUdpSocket));
    }

// ---- 5. tryInferNetworkCall (named import) ---------------------------------
    if (id.name == "createUdpSocket") {
        inferArgs(call);
        if (call.arguments.size() != 1 || !call.arguments.front()) {
            report(diagcode::kArityMismatch, anchorSpan(call.span),
                   "createUdpSocket(type) takes exactly one argument (\"udp4\" or \"udp6\")");
        }
        out = networkObjectType(kUdpSocket);
        return true;
    }

// ---- 6. Member docs (networkObjectMemberDoc, next to WebSocket docs) -------
    if (type == "UdpSocket" && member == "send") return {"send(data: Buffer | string, port: Port, address: string): void", "Sends one UDP datagram to a numeric address. Fire-and-forget; send failures arrive via the \"error\" event."};
    if (type == "UdpSocket" && member == "bind") return {"bind(port: Port, address?: string): void", "Binds the socket (wildcard address of its family when omitted) and starts receiving. Port 0 requests an ephemeral port."};
    if (type == "UdpSocket" && member == "close") return {"close(): void", "Stops receiving and closes the socket; the \"close\" event fires once."};
    if (type == "UdpSocket" && member == "address") return {"address(): UdpRemoteInfo", "The bound address (throws when not bound)."};
    if (type == "UdpRemoteInfo" && member == "address") return {"address: string", "The sender's IP address as text."};
    if (type == "UdpRemoteInfo" && member == "port") return {"port: Port", "The sender's port."};
    if (type == "UdpRemoteInfo" && member == "family") return {"family: string", "\"IPv4\" or \"IPv6\"."};
