// =============================================================================
// PASTE SNIPPETS for src/std/network/codegen.cpp (+ codegen_methods.cpp for
// the member-call table) - UDP sub-surface.
//
// OWNERSHIP: the std/network family (src/std/network/udp/**). NOT compiled
// directly; the lead pastes the blocks below.
// Lowering target: cp3/std/network/udp.hpp (namespace
// ::cp3::runtime::network::). Generated code must #include that header when
// needsNetwork_ is set via a UDP site (the include line is emitted by the
// generator's needsNetwork_ path; add "cp3/std/network/udp.hpp" next to
// "cp3/std/network.hpp" there, or emit it from SetNeedsNetwork).
// =============================================================================

// ---- 1. Marker constants (next to kWebSocket / kWebSocketServer) ----------
constexpr const char* kUdpSocket = "$network:UdpSocket";
constexpr const char* kUdpRemoteInfo = "$network:UdpRemoteInfo";

// ---- 2. Receiver recognition: add to Generator::isNetworkObject ----------
        name == kUdpSocket || name == kUdpRemoteInfo ||

// ---- 3. Named import: `createUdpSocket("udp4")` (emitNetworkIdentifier) ---
    if (name == "createUdpSocket") {
        needsNetwork_ = true;
        needsEventLoop_ = true;
        if (c.arguments.empty() || !c.arguments.front()) {
            reportCodegenError(c, diagcode::kArityMismatch,
                               "createUdpSocket(type) takes one argument");
            return "0.0";
        }
        return std::string(kRuntimeNs) + "createUdpSocket(" + expr(*c.arguments.front()) + ")";
    }

// ---- 4. Namespace corner: `network.createUdpSocket(...)` ------------------
    if (m.property == "createUdpSocket") return emitNetworkIdentifier(c, "createUdpSocket");

// ---- 5. Member calls on a $network:UdpSocket receiver ---------------------
// (in the codegen_methods.cpp member table, mirroring ws.on/send/close;
//  the handler arrows are emitted with declared parameter types (contextParams):
//  "message" -> (const ::cp3::runtime::buffer::Buffer&, ::cp3::runtime::network::UdpRemoteInfo),
//  "error"   -> (Thrown), "close"/"listening" -> ())
//
//   socket.on("message", h) -> socket.on("message", std::function<void(const ::cp3::runtime::buffer::Buffer&, ::cp3::runtime::network::UdpRemoteInfo)>(h))
//   socket.on("error", h)   -> socket.on("error", std::function<void(Thrown)>(h))
//   socket.on("close", h)   -> socket.on("close", std::function<void()>(h))
//   socket.bind(port)       -> socket.bind(port)
//   socket.bind(port, addr) -> socket.bind(port, addr)
//   socket.send(data, port, addr)
//                           -> socket.send(data, port, addr)
//                              (data is already a Buffer or std::string; both
//                               overloads exist, so no conversion wrapper)
//   socket.close()          -> socket.close()
//   socket.bound()          -> socket.bound()
//   socket.address()        -> socket.address()
//   from.address / from.port / from.family -> plain member reads on UdpRemoteInfo
