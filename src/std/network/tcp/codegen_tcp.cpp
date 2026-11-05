// =============================================================================
// src/std/network/tcp/codegen_tcp.inc - PASTE FRAGMENTS for
// src/std/network/codegen/codegen.cpp + codegen_methods.cpp (lead applies;
// this file is not compiled). CP3 spellings -> the tcp.hpp surface, via
// ::cp3::runtime::network::tcp:: (kRuntimeNs for that namespace).
// =============================================================================
// --- A. codegen.cpp: markers + isNetworkObject --------------------------------
// constexpr const char* kTcpSocket = "$network:TcpSocket";
// constexpr const char* kTcpServer = "$network:TcpServer";
// Add both names to isNetworkObject()'s marker list.
//
// --- B. codegen.cpp: emitNetworkIdentifier (bare calls) ------------------------
// if (name == "createTcpServer") {
//     needsNetwork_ = true; needsEventLoop_ = true; needsTcp_ = true; // see D
//     std::string options = std::string(kTcpNs) + "TcpServerOptions{}";
//     if (!c.arguments.empty() && c.arguments.front() &&
//         c.arguments.front()->kind == NodeKind::ObjectLiteral) {
//         const auto& lit = static_cast<const ObjectLiteral&>(*c.arguments.front());
//         std::string inner;
//         for (const auto& p : lit.properties) {
//             if (!p || !p->value) continue;
//             const std::string key = objectKey(*p);
//             if (key == "threads" || key == "maxConnections" ||
//                 key == "idleTimeout" || key == "maxWriteBuffer") {
//                 inner += "." + key + " = " + expr(*p->value) + "; ";
//             } else {
//                 reportTypeError(*p->value,
//                     "type mismatch: unknown createTcpServer() option '" + key + "'");
//             }
//         }
//         options = "[&] { " + std::string(kTcpNs) + "TcpServerOptions o; " +
//                   inner + "return o; }()";
//     } else if (!c.arguments.empty() && c.arguments.front()) {
//         reportTypeError(*c.arguments.front(),
//             "type mismatch: createTcpServer() options must be an object literal");
//     }
//     return std::string(kTcpNs) + "createTcpServer(" + options + ")";
// }
// if (name == "connectTcp") {
//     needsNetwork_ = true; needsEventLoop_ = true; needsTcp_ = true;
//     if (c.arguments.empty() || !c.arguments.front()) {
//         reportCodegenError(c, diagcode::kArityMismatch,
//             "connectTcp(options) takes one argument");
//         return "0.0";
//     }
//     const Expr* first = c.arguments.front().get();
//     if (first->kind == NodeKind::ObjectLiteral) {
//         const auto& lit = static_cast<const ObjectLiteral&>(*first);
//         std::string inner;
//         for (const auto& p : lit.properties) {
//             if (!p || !p->value) continue;
//             const std::string key = objectKey(*p);
//             if (key == "host") inner += ".host = " + expr(*p->value) + "; ";
//             else if (key == "port") inner += ".port = " + expr(*p->value) + "; ";
//             else if (key == "timeout") inner += ".timeout = " + expr(*p->value) + "; ";
//             else if (key == "idleTimeout") inner += ".idleTimeout = " + expr(*p->value) + "; ";
//             else if (key == "maxWriteBuffer") inner += ".maxWriteBuffer = " + expr(*p->value) + "; ";
//             else reportTypeError(*p->value,
//                 "type mismatch: unknown connectTcp() option '" + key + "'");
//         }
//         return std::string(kTcpNs) + "connectTcp([&] { " +
//                std::string(kTcpNs) + "TcpConnectOptions o; " + inner + "return o; }())";
//     }
//     // connectTcp(host, port, timeout?) convenience spelling.
//     std::string args = expr(*first);
//     if (c.arguments.size() >= 2 && c.arguments[1]) args += ", " + expr(*c.arguments[1]);
//     if (c.arguments.size() >= 3 && c.arguments[2]) args += ", " + expr(*c.arguments[2]);
//     return std::string(kTcpNs) + "connectTcp(" + args + ")";
// }
// Also route `network.createTcpServer(...)` / `network.connectTcp(...)` in
// emitNetworkCall to the two arms above (like createServer / lookup).
//
// --- C. codegen.cpp: emitNetworkNew -------------------------------------------
// if (name == "TcpSocket") {
//     needsNetwork_ = true; needsEventLoop_ = true; needsTcp_ = true;
//     // new TcpSocket(host, port, timeout?)
//     std::string args;
//     for (std::size_t i = 0; i < n.arguments.size() && i < 3; ++i) {
//         if (!n.arguments[i]) continue;
//         if (!args.empty()) args += ", ";
//         args += expr(*n.arguments[i]);
//     }
//     return std::string(kTcpNs) + "TcpSocket(" + args + ")";
// }
// if (name == "TcpServer") { /* same options lowering as createTcpServer */ }
//
// --- D. codegen_methods.cpp: event params + arg context -----------------------
// eventParams() additions (the lambda parameter types per event name):
//   if (event == "data")    return {"const std::string&"};   // or Buffer slot:
//   if (event == "data" && <handler param is Buffer>) handled by overload
//       resolution: the string and Buffer std::function overloads on
//       TcpSocket::on make the generated lambda pick its slot by arity/type.
//   if (event == "error")   return {std::string(kThrownType)};  // Error object
//   if (event == "connection") return {std::string("::cp3::runtime::network::tcp::TcpSocket&")};
//   // "end" / "close" / "drain" / "connect" / "listening": {} (no params)
// contextType() addition:
//   if (cpp == "::cp3::runtime::network::tcp::TcpSocket&" ||
//       cpp == "::cp3::runtime::network::tcp::TcpSocket")
//       return Type::named(TypeKind::Object, kTcpSocket);
// networkArg(): TcpSocket/TcpServer receivers take the eventParams context
// for index > 0 exactly like the WebSocket receiver does.
// Member reads (networkFieldType): TcpSocket has no data fields; all members
// are calls, so the generic member-call path (receiver + networkArgs) is
// enough. `write` must NOT be treated as an HttpResponse write (name clash
// is resolved by receiver marker, which is why kTcpSocket registration in A
// matters).
//
// --- E. include emission -------------------------------------------------------
// Wherever needsNetwork_ emits #include "cp3/std/network.hpp", also emit
// #include "cp3/std/network/tcp.hpp" when needsTcp_ is set (new bool on
// Generator, set in B/C and in the member-call path for kTcpSocket /
// kTcpServer receivers).
