// =============================================================================
// src/std/network/runtime/runtime_dns.cpp - `std/network` dns.lookup(hostname).
//
// OWNERSHIP: the std/network family (src/std/network/**). Selected by the
// `dnsResolve(` marker so a program that only calls `lookup` does not compile
// or link the fetch transport (runtime_fetch_io.cpp), whose response parser
// lives in runtime_fetch.cpp.
//
// A one-shot libuv resolution that settles a promise with the first address's
// text form. The fetch client owns its own callbacks; this is the
// promise-only sibling.
// =============================================================================
#include "cp3/std/network.hpp"

#include <memory>
#include <string>

#include <uv.h>

#include "runtime_loop.hpp"

namespace cp3::runtime::network::detail {
namespace dns_detail {
struct DnsOp {
    uv_getaddrinfo_t dns{};
    Promise<std::string> promise;
    std::string host;
};

inline void onDnsResolved(uv_getaddrinfo_t* req, int status,
                          struct addrinfo* res) {
    std::unique_ptr<DnsOp> op(static_cast<DnsOp*>(req->data));
    if (status != 0 || res == nullptr) {
        if (res != nullptr) uv_freeaddrinfo(res);
        op->promise.rejectWith(
            Thrown::make("dns.lookup: could not resolve '" + op->host + "'"));
    } else {
        char text[80]{};
        if (res->ai_family == AF_INET) {
            const auto* addr =
                reinterpret_cast<const struct sockaddr_in*>(res->ai_addr);
            uv_ip4_name(addr, text, sizeof(text));
        } else if (res->ai_family == AF_INET6) {
            const auto* addr =
                reinterpret_cast<const struct sockaddr_in6*>(res->ai_addr);
            uv_ip6_name(addr, text, sizeof(text));
        }
        uv_freeaddrinfo(res);
        op->promise.fulfill(std::string(text));
    }
    // The retain in dnsResolve() keeps the event loop alive for this callback;
    // release it once the promise has settled, exactly like fetch's releaseOp.
    releaseNetwork();
}
}  // namespace dns_detail
}  // namespace cp3::runtime::network::detail

namespace cp3::runtime::network {

Promise<std::string> dnsResolve(const std::string& host) {
    Promise<std::string> promise;
    uv_loop_t* loop = static_cast<uv_loop_t*>(loopHandle());
    if (loop == nullptr) {
        return Promise<std::string>::reject(
            Thrown::make("dns.lookup: could not create the libuv loop"));
    }
    retainNetwork();
    auto* op = new detail::dns_detail::DnsOp{};
    op->host = host;
    op->promise = promise;
    op->dns.data = op;
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    const int rc = uv_getaddrinfo(loop, &op->dns,
                                  detail::dns_detail::onDnsResolved,
                                  host.c_str(), nullptr, &hints);
    if (rc != 0) {
        promise.rejectWith(
            Thrown::make("dns.lookup could not start for '" + host + "'"));
        releaseNetwork();
        delete op;
        return promise;
    }
    return promise;
}

}  // namespace cp3::runtime::network
