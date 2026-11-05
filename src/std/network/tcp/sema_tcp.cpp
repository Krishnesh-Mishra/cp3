// =============================================================================
// src/std/network/tcp/sema_tcp.inc - PASTE FRAGMENT for
// src/std/network/codegen/sema.cpp (lead applies; this file is not compiled).
//
// Add the markers, field rows, networkMember entries and the two
// tryInfer arms below, mirroring the WebSocket rows. Field names are the
// CP3 spellings; types follow the documented surface in tcp.hpp.
// =============================================================================
// --- 1. Markers (next to kWebSocket / kWebSocketServer) ---------------------
// const char* const kTcpSocket = "$network:TcpSocket";
// const char* const kTcpServer = "$network:TcpServer";

// --- 2. Field rows (next to webSocketFields / webSocketServerFields) ---------
// std::vector<Field> tcpSocketFields() {
//     return {
//         Field{"on", Type::function({Type::string(), eventListener()}, Type::voidType()), false},
//         Field{"write", Type::function({Type::unionOf({Type::string(), bufferType()})}, Type::boolean()), false},
//         Field{"end", withOptionalTail(Type::function({Type::unionOf({Type::string(), bufferType()})}, Type::voidType())), false},
//         Field{"close", Type::function({}, Type::voidType()), false},
//         Field{"pause", Type::function({}, Type::voidType()), false},
//         Field{"resume", Type::function({}, Type::voidType()), false},
//         Field{"isPaused", Type::function({}, Type::boolean()), false},
//         Field{"isClosed", Type::function({}, Type::boolean()), false},
//         Field{"isConnected", Type::function({}, Type::boolean()), false},
//         Field{"bufferedBytes", Type::function({}, Type::number()), false},
//         Field{"remoteAddress", Type::function({}, Type::string()), false},
//         Field{"localAddress", Type::function({}, Type::string()), false},
//         Field{"remotePort", Type::function({}, Type::number()), false},
//         Field{"localPort", Type::function({}, Type::number()), false},
//     };
// }
// std::vector<Field> tcpServerFields(TypeSystem ts) {
//     return {
//         Field{"on", Type::function({Type::string(), eventListener()}, Type::voidType()), false},
//         Field{"listen", withOptionalTail2(Type::function({stdIntegerType(StdIntegerKind::Port, ts), Type::string(), callbackOf(0)}, Type::voidType())), false},
//         Field{"close", Type::function({}, Type::voidType()), false},
//         Field{"listening", Type::function({}, Type::boolean()), false},
//         Field{"addressPort", Type::function({}, Type::number()), false},
//         Field{"connectionCount", Type::function({}, Type::number()), false},
//     };
// }
// Register both in networkFieldsFor:
//   if (name == kTcpSocket) return tcpSocketFields();
//   if (name == kTcpServer) return tcpServerFields(ts);

// --- 3. Namespace members (Analyzer::networkMember) --------------------------
// if (prop == "createTcpServer") {
//     return withOptionalTail(Type::function({ambient()}, networkObjectType(kTcpServer)));
// }
// if (prop == "connectTcp") {
//     return Type::function({ambient()}, networkObjectType(kTcpSocket));
// }
// if (prop == "TcpSocket") {
//     return Type::function({Type::string(), Type::number(), Type::number()}, networkObjectType(kTcpSocket));
// }
// if (prop == "TcpServer") {
//     return withOptionalTail(Type::function({ambient()}, networkObjectType(kTcpServer)));
// }

// --- 4. Bare-call inference (Analyzer::tryInferNetworkCall) ------------------
// if (id.name == "createTcpServer") {
//     inferArgs(call);  // options object checked at codegen (threads etc.)
//     out = networkObjectType(kTcpServer);
//     return true;
// }
// if (id.name == "connectTcp") {
//     inferArgs(call);
//     if (call.arguments.empty()) {
//         report(diagcode::kArityMismatch, anchorSpan(call.span),
//                "connectTcp(options) takes one argument");
//     }
//     out = networkObjectType(kTcpSocket);
//     return true;
// }

// --- 5. Construct inference (Analyzer::inferNetworkConstruct) ----------------
// if (name == "TcpSocket") {
//     for (auto& arg : args) { if (arg) inferExpr(*arg); }
//     return networkObjectType(kTcpSocket);
// }
// if (name == "TcpServer") {
//     for (auto& arg : args) { if (arg) inferExpr(*arg); }
//     return networkObjectType(kTcpServer);
// }
// Also add {"TcpSocket", "std/network"} and {"TcpServer", "std/network"} to
// the std_check.cpp class table next to the WebSocket rows, so
// `import { TcpSocket, TcpServer } from "std/network"` resolves.
