// =============================================================================
// src/std/network/dns/runtime/runtime_dns_lookup.cpp - `std/network`
// dns.lookup(hostname).
//
// OWNERSHIP: the std/network/dns family (src/std/network/dns/**). Selected
// by the `dns::lookup(` marker in kNetworkNativePackage
// (src/std/network/codegen/native.hpp, see dns/native.hpp), so a program
// that only calls `lookup` does not compile or link the fetch transport
// (runtime_fetch_io.cpp), whose response parser lives in runtime_fetch.cpp.
//
// A one-shot libuv resolution that settles a Promise<std::string> with the
// first address's text form. It mirrors the two existing getaddrinfo users:
// fetch (runtime_fetch_io.cpp: onResolved + beginFetch) and the previous
// dns runtime (runtime/runtime_dns.cpp), but rejects with genuine Error
// objects (Thrown::makeError) instead of boxed strings, per the v0.2 error
// model ("Runtime and standard-library failures throw Error objects, never
// raw strings"). The loop is the shared per-thread libuv loop
// (../runtime/runtime_loop.hpp), the same one fetch drives.
// =============================================================================
#include "cp3/std/network/dns.hpp"

#include <memory>
#include <string>

#include <uv.h>

#include "../runtime/runtime_loop.hpp"

namespace cp3::runtime::network::dns {
namespace {

struct DnsOp {
    uv_getaddrinfo_t dns{};
    Promise<std::string> promise;
    std::string host;
};

/// The rejection every failure path settles with: a genuine Error object,
/// boxed through makeError so `await` re-throws a cp3 Error (ROADMAP §1).
/// `detail` is "" when there is nothing to add (e.g. a null result list).
Thrown dnsError(const std::string& message, const std::string& detail) {
    return Thrown::makeError(std::make_shared<::cp3::Error>(
        detail.empty() ? message : message + " (" + detail + ")"));
}

void onDnsResolved(uv_getaddrinfo_t* req, int status,
                   struct addrinfo* res) {
    std::unique_ptr<DnsOp> op(static_cast<DnsOp*>(req->data));
    if (status != 0 || res == nullptr) {
        if (res != nullptr) uv_freeaddrinfo(res);
        const std::string detail =
            status != 0 ? std::string(uv_strerror(status)) : std::string();
        op->promise.rejectWith(dnsError(
            "dns.lookup: could not resolve '" + op->host + "'", detail));
    } else {
        char text[80]{};
        int nameRc = -1;
        if (res->ai_addr != nullptr && res->ai_family == AF_INET) {
            const auto* addr =
                reinterpret_cast<const struct sockaddr_in*>(res->ai_addr);
            nameRc = uv_ip4_name(addr, text, sizeof(text));
        } else if (res->ai_addr != nullptr && res->ai_family == AF_INET6) {
            const auto* addr =
                reinterpret_cast<const struct sockaddr_in6*>(res->ai_addr);
            nameRc = uv_ip6_name(addr, text, sizeof(text));
        }
        uv_freeaddrinfo(res);
        // Name it only when libuv actually rendered an address: an unknown
        // family (or a failed uv_ip*_name) must reject, not fulfil with "".
        if (nameRc != 0 || text[0] == '\0') {
            op->promise.rejectWith(dnsError(
                "dns.lookup: could not resolve '" + op->host + "'", ""));
        } else {
            op->promise.fulfill(std::string(text));
        }
    }
    // The retain in lookup() keeps the event loop alive for this callback;
    // release it once the promise has settled, exactly like fetch's
    // releaseOp / the previous dns runtime.
    releaseNetwork();
}

}  // namespace

Promise<std::string> lookup(const std::string& host) {
    Promise<std::string> promise;
    uv_loop_t* loop = static_cast<uv_loop_t*>(loopHandle());
    if (loop == nullptr) {
        return Promise<std::string>::reject(
            dnsError("dns.lookup: could not create the libuv loop", ""));
    }
    retainNetwork();
    auto* op = new DnsOp{};
    op->host = host;
    op->promise = promise;
    op->dns.data = op;
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    const int rc = uv_getaddrinfo(loop, &op->dns, onDnsResolved,
                                  host.c_str(), nullptr, &hints);
    if (rc != 0) {
        promise.rejectWith(dnsError(
            "dns.lookup could not start for '" + host + "'",
            std::string(uv_strerror(rc))));
        releaseNetwork();
        delete op;
        return promise;
    }
    return promise;
}

}  // namespace cp3::runtime::network::dns
