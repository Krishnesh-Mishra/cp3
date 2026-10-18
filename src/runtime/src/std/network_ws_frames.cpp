// =============================================================================
// src/runtime/src/std/network_ws_frames.cpp - `std/network` WebSocket protocol.
//
// OWNERSHIP: the network wave. The pure RFC 6455 pieces of the hand-rolled
// client (randomness, base64, SHA-1, frame encoding, header lookup), split out
// of network_ws_client.cpp so both files stay under the source-size budget.
// The transport and the frame parser stay in network_ws_client.cpp; nothing
// here touches libuv or the event loop.
// =============================================================================
#include "network_ws.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace cp3::runtime::network::detail {
namespace {

std::uint32_t rotateLeft(std::uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
}

/// SHA-1 (RFC 3174); only used to verify the handshake accept token.
void sha1(const std::uint8_t* data, std::size_t size, std::uint8_t out[20]) {
    std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::vector<std::uint8_t> message(data, data + size);
    message.push_back(0x80u);
    while (message.size() % 64 != 56) message.push_back(0);
    const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8u;
    for (int i = 7; i >= 0; --i) {
        message.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFu));
    }
    std::uint32_t words[80];
    for (std::size_t offset = 0; offset < message.size(); offset += 64) {
        for (int i = 0; i < 16; ++i) {
            words[i] =
                (static_cast<std::uint32_t>(message[offset + 4 * i]) << 24) |
                (static_cast<std::uint32_t>(message[offset + 4 * i + 1]) << 16) |
                (static_cast<std::uint32_t>(message[offset + 4 * i + 2]) << 8) |
                static_cast<std::uint32_t>(message[offset + 4 * i + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            words[i] = rotateLeft(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const std::uint32_t temp = rotateLeft(a, 5) + f + e + k + words[i];
            e = d;
            d = c;
            c = rotateLeft(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[4 * i + j] = static_cast<std::uint8_t>((h[i] >> (24 - 8 * j)) & 0xFFu);
        }
    }
}

}  // namespace

void randomBytes(std::uint8_t* out, std::size_t size) {
    static std::mt19937_64 generator{std::random_device{}()};
    std::uint64_t word = 0;
    for (std::size_t i = 0; i < size; ++i) {
        if (i % 8 == 0) word = generator();
        out[i] = static_cast<std::uint8_t>((word >> (8 * (i % 8))) & 0xFFu);
    }
}

std::string base64Encode(const std::uint8_t* data, std::size_t size) {
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    for (std::size_t i = 0; i < size; i += 3) {
        const std::uint32_t b0 = data[i];
        const std::uint32_t b1 = i + 1 < size ? data[i + 1] : 0;
        const std::uint32_t b2 = i + 2 < size ? data[i + 2] : 0;
        const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kAlphabet[(triple >> 18) & 0x3Fu]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3Fu]);
        out.push_back(i + 1 < size ? kAlphabet[(triple >> 6) & 0x3Fu] : '=');
        out.push_back(i + 2 < size ? kAlphabet[triple & 0x3Fu] : '=');
    }
    return out;
}

std::string wsAcceptToken(const std::string& key) {
    const std::string digestInput = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::uint8_t digest[20];
    sha1(reinterpret_cast<const std::uint8_t*>(digestInput.data()), digestInput.size(), digest);
    return base64Encode(digest, sizeof(digest));
}

std::string wsMakeFrame(std::uint8_t opcode, const std::string& payload) {
    std::string frame;
    frame.push_back(static_cast<char>(0x80u | opcode));
    const std::size_t size = payload.size();
    if (size < 126) {
        frame.push_back(static_cast<char>(0x80u | static_cast<std::uint8_t>(size)));
    } else if (size <= 0xFFFFu) {
        frame.push_back(static_cast<char>(0x80u | 126u));
        frame.push_back(static_cast<char>((size >> 8) & 0xFFu));
        frame.push_back(static_cast<char>(size & 0xFFu));
    } else {
        frame.push_back(static_cast<char>(0x80u | 127u));
        for (int i = 7; i >= 0; --i) {
            frame.push_back(
                static_cast<char>((static_cast<std::uint64_t>(size) >> (8 * i)) & 0xFFu));
        }
    }
    std::uint8_t mask[4] = {0, 0, 0, 0};
    randomBytes(mask, 4);
    frame.append(reinterpret_cast<const char*>(mask), 4);
    const std::size_t start = frame.size();
    frame.append(payload);
    for (std::size_t i = 0; i < size; ++i) {
        frame[start + i] =
            static_cast<char>(static_cast<std::uint8_t>(frame[start + i]) ^ mask[i & 3]);
    }
    return frame;
}

std::string wsHeaderValue(const std::string& head, const std::string& name) {
    const std::string needle = "\r\n" + name + ":";
    std::string lower = head;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    const std::size_t at = lower.find(needle);
    if (at == std::string::npos) return {};
    const std::size_t valueStart = at + needle.size();
    const std::size_t valueEnd = head.find("\r\n", valueStart);
    std::string value = head.substr(valueStart, valueEnd - valueStart);
    const std::size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

}  // namespace cp3::runtime::network::detail
