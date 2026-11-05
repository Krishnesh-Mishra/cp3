# UDP sub-surface — wiring (for the lead; nothing here is wired yet)

All files live in new, separate files under `src/std/network/udp/**`.
No existing file was touched. Code only: not built, not tested, not committed.

## Files

| File | What |
|---|---|
| `include/cp3/std/network/udp.hpp` | Public contract: `UdpSocket`, `UdpRemoteInfo`, `createUdpSocket()`. |
| `runtime_udp.hpp` | Private impl decl (`UdpImpl`, slots, address helpers). |
| `runtime_udp.cpp` | libuv `uv_udp_t` implementation. |
| `sema_udp.inc` | Paste snippets for `src/std/network/sema.cpp`. |
| `codegen_udp.inc` | Paste snippets for `src/std/network/codegen.cpp` (+ `codegen_methods.cpp` member table). |
| `native_udp.inc` | Paste lines for `src/std/network/codegen/native.hpp`. |
| `WIRING.md` | This file. |

## Export surface

```cpp
namespace cp3::runtime::network {

struct UdpRemoteInfo { std::string address; std::uint16_t port; std::string family; }; // IPv4|IPv6

UdpSocket createUdpSocket(const std::string& type); // "udp4" | "udp6", else Thrown

class UdpSocket {
    explicit UdpSocket(std::string type);
    void on(const std::string& event,
            std::function<void(const buffer::Buffer&, const UdpRemoteInfo&)>) const; // "message"
    void on(const std::string& event, std::function<void(Thrown)>) const;            // "error"
    void on(const std::string& event, std::function<void()>) const;                  // "close" | "listening"
    void bind(double port) const;                                                     // wildcard addr of family
    void bind(double port, const std::string& address) const;
    void bind(double port, std::function<void()> callback) const;
    void send(const buffer::Buffer& data, double port, const std::string& address) const;
    void send(const std::string& data, double port, const std::string& address) const;
    void close() const;          // idempotent
    bool bound() const;
    UdpRemoteInfo address() const; // getsockname; throws when unbound
};
}
```

cp3 spelling: `createUdpSocket("udp4"|"udp6")`, `socket.bind(port[, address])`,
`socket.send(Buffer|string, port, address)`, `socket.on("message", (msg, from) => …)`
where `from = { address, port, family }`, `socket.close()`.

## Wiring lines (apply in this order)

1. **native.hpp** (`src/std/network/codegen/native.hpp`): apply `native_udp.inc`
   — add `"src/std/network/udp/include"` to includeRoots,
   `"src/std/network/udp/runtime_udp.cpp"` to runtimeSources, and the parallel
   marker `"createUdpSocket("` to the markers array (same length/order).
2. **sema.cpp** (`src/std/network/sema.cpp`): apply `sema_udp.inc` — marker
   constants, `udpSocketFields`/`udpRemoteInfoFields`, `networkFieldsFor`
   dispatch, `networkMember` (`createUdpSocket`), `tryInferNetworkCall`, docs.
3. **codegen.cpp** (`src/std/network/codegen/codegen.cpp`): apply
   `codegen_udp.inc` — markers, `isNetworkObject`, `emitNetworkIdentifier`,
   namespace corner; plus the `UdpSocket` member table in
   `codegen_methods.cpp`. Ensure generated code includes
   `cp3/std/network/udp.hpp` when a UDP site sets `needsNetwork_`.
4. **network.hpp** (optional, not required): `#include "cp3/std/network/udp.hpp"`
   is *not* needed — the UDP header is standalone and codegen emits it
   directly. Do not add `UdpSocket` to `cp3/std/network.hpp` (would break the
   "new separate files" boundary).

## Read-buffer design (the requested reuse)

- One `thread_local std::vector<char>` of 64 KiB per thread is the only recv
  buffer; the libuv alloc callback returns it verbatim and never allocates.
- One `thread_local sockaddr_storage` per thread is the only recv address.
- The recv callback makes exactly one copy (into the owning `Buffer` the
  listener receives) so the datagram survives the next recv overwriting the
  shared buffer. No `uv_buf`, handle state, or address storage is allocated
  per packet. Sends allocate one `SendRequest` per `send()` (payload copy
  libuv must see until its callback) — that is per-send, not per-recv.

## Limitations

- **IP literals only**: `bind`/`send` addresses must be numeric IPv4/IPv6
  literals for the socket family (`uv_ip4_addr`/`uv_ip6_addr`); hostnames are
  rejected (no DNS on the UDP path). A udp4 socket cannot send to an IPv6
  literal and vice versa.
- **64 KiB truncation**: datagrams larger than the per-thread read buffer
  truncate at the buffer boundary (no reassembly, no grow-on-demand).
- **Fire-and-forget send**: no per-send callback/promise; async send failures
  arrive via the `"error"` event. Synchronous misuse (bad port, unparseable
  address, closed socket) throws.
- **One handler per event** (like `WebSocket`): re-registering replaces.
- **Single loop thread**: all callbacks run on the runtime/libuv thread that
  created the socket; not safe to call from arbitrary OS threads.
- **No multicast / broadcast options**, no `setMulticastTTL`/`setMulticastLoopback`,
  no `uv_udp_set_membership`, no dual-stack (`IPV6_V6ONLY` untouched), no
  `tryBind`/`connect`/`disconnect`, no packet-info (`uv_udp_recv` flags ignored).
- **`address()` throws when unbound** (no Node-style `{}` return).
- Not built/tested (per instructions): needs the lead's cmake/build/tests pass.
