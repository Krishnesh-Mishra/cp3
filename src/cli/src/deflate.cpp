// =============================================================================
// cli/deflate.cpp - raw DEFLATE compressor DEFINITIONS (RFC 1951).
//
// OWNERSHIP: cli module (src/cli/**). Implements cli::deflateRaw
// (private/deflate.hpp).
//
// Strategy: fixed Huffman codes (BTYPE=01) with greedy LZ77 matching over the
// 32 KiB window, hash chains bounded per position. Fixed codes skip the
// per-block Huffman-table construction entirely; every conforming inflater
// reads the result. Inputs above the stored-block threshold fall back to
// stored blocks so memory stays bounded on huge files.
// =============================================================================
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "private/deflate.hpp"
#include "private/deflate_tables.hpp"

namespace cp3::cli {

namespace {

constexpr std::size_t kWindow = 32768;
constexpr std::size_t kWindowMask = kWindow - 1;
constexpr std::size_t kHashBits = 15;
constexpr std::size_t kHashSize = std::size_t{1} << kHashBits;
constexpr std::size_t kMinMatch = 3;
constexpr std::size_t kMaxMatch = 258;
constexpr int kMaxChain = 64;
constexpr std::size_t kStoredThreshold = 64u * 1024u * 1024u;
constexpr std::uint32_t kNil = 0xFFFFFFFFu;

// Accumulates LSB-first bits and flushes whole bytes.
struct BitWriter {
    std::string out;
    std::uint32_t buf = 0;
    int bits = 0;

    void put(std::uint32_t value, int n) {
        buf |= (value & ((1u << n) - 1u)) << bits;
        bits += n;
        while (bits >= 8) {
            out.push_back(static_cast<char>(buf & 0xFF));
            buf >>= 8;
            bits -= 8;
        }
    }

    // Pads the partial byte with zeros (used before stored-block payloads).
    void alignByte() {
        if (bits > 0) {
            out.push_back(static_cast<char>(buf & 0xFF));
            buf = 0;
            bits = 0;
        }
    }
};

std::uint32_t reverseBits(std::uint32_t value, int n) {
    std::uint32_t out = 0;
    for (int i = 0; i < n; ++i) {
        out = (out << 1) | (value & 1u);
        value >>= 1;
    }
    return out;
}

// Fixed literal/length code table (RFC 1951 §3.2.6): codes are transmitted
// MSB-first, the bit sink is LSB-first, so every code is bit-reversed here.
void putLitLen(BitWriter& bw, int sym) {
    if (sym <= 143) {
        bw.put(reverseBits(static_cast<std::uint32_t>(0x30 + sym), 8), 8);
    } else if (sym <= 255) {
        bw.put(reverseBits(static_cast<std::uint32_t>(0x190 + sym - 144), 9), 9);
    } else if (sym <= 279) {
        bw.put(reverseBits(static_cast<std::uint32_t>(sym - 256), 7), 7);
    } else {
        bw.put(reverseBits(static_cast<std::uint32_t>(0xC0 + sym - 280), 8), 8);
    }
}

void putDist(BitWriter& bw, int sym) {
    bw.put(reverseBits(static_cast<std::uint32_t>(sym), 5), 5);
}

int lengthCode(std::uint32_t len) {
    for (int i = 28; i >= 0; --i) {
        if (len >= kLenBase[i]) return i;
    }
    return 0;
}

int distCode(std::uint32_t dist) {
    for (int i = 29; i >= 0; --i) {
        if (dist >= kDistBase[i]) return i;
    }
    return 0;
}

void emitMatch(BitWriter& bw, std::uint32_t len, std::uint32_t dist) {
    const int lc = lengthCode(len);
    putLitLen(bw, 257 + lc);
    bw.put(len - kLenBase[lc], kLenExtra[lc]);
    const int dc = distCode(dist);
    putDist(bw, dc);
    bw.put(dist - kDistBase[dc], kDistExtra[dc]);
}

// Greedy LZ77 matcher: hash chains into a ring buffer. A stale chain entry
// (one overwritten by a newer position) is detected by the strictly-decreasing
// check and ends that chain - matches are validated by comparing bytes, so a
// broken chain can only cost ratio, never correctness.
class Matcher {
public:
    explicit Matcher(std::string_view input)
        : data_(reinterpret_cast<const unsigned char*>(input.data())),
          size_(input.size()) {
        head_.fill(kNil);
        prev_.fill(kNil);
    }

    void insert(std::size_t pos) {
        if (pos + kMinMatch > size_) return;
        const std::size_t h = hash3(pos);
        prev_[pos & kWindowMask] = head_[h];
        head_[h] = static_cast<std::uint32_t>(pos);
    }

    void find(std::size_t pos, int& bestLen, int& bestDist) const {
        bestLen = 0;
        bestDist = 0;
        if (pos + kMinMatch > size_) return;
        const std::size_t maxLen = std::min(kMaxMatch, size_ - pos);
        std::uint32_t cand = head_[hash3(pos)];
        for (int chain = 0; cand != kNil && chain < kMaxChain; ++chain) {
            const std::size_t c = cand;
            if (c >= pos || pos - c > kWindow) break;
            if (static_cast<std::size_t>(bestLen) < maxLen &&
                data_[c + bestLen] == data_[pos + bestLen]) {
                std::size_t len = 0;
                while (len < maxLen && data_[c + len] == data_[pos + len]) ++len;
                if (static_cast<int>(len) > bestLen) {
                    bestLen = static_cast<int>(len);
                    bestDist = static_cast<int>(pos - c);
                }
            }
            const std::uint32_t next = prev_[c & kWindowMask];
            if (next >= c) break;
            cand = next;
        }
    }

private:
    std::size_t hash3(std::size_t pos) const {
        const std::uint32_t a = data_[pos];
        const std::uint32_t b = data_[pos + 1];
        const std::uint32_t c = data_[pos + 2];
        return ((a << 10) ^ (b << 5) ^ c) & (kHashSize - 1);
    }

    const unsigned char* data_;
    std::size_t size_;
    std::array<std::uint32_t, kHashSize> head_;
    std::array<std::uint32_t, kWindow> prev_;
};

std::string deflateFixed(std::string_view input) {
    BitWriter bw;
    bw.put(1, 1);  // BFINAL
    bw.put(1, 2);  // BTYPE = fixed Huffman
    Matcher matcher(input);
    std::size_t pos = 0;
    while (pos < input.size()) {
        int len = 0;
        int dist = 0;
        matcher.find(pos, len, dist);
        if (len >= static_cast<int>(kMinMatch)) {
            emitMatch(bw, static_cast<std::uint32_t>(len),
                      static_cast<std::uint32_t>(dist));
            for (int i = 0; i < len; ++i) {
                matcher.insert(pos + static_cast<std::size_t>(i));
            }
            pos += static_cast<std::size_t>(len);
        } else {
            putLitLen(bw, static_cast<unsigned char>(input[pos]));
            matcher.insert(pos);
            ++pos;
        }
    }
    putLitLen(bw, 256);  // end of block
    bw.alignByte();
    return std::move(bw.out);
}

std::string deflateStored(std::string_view input) {
    BitWriter bw;
    std::size_t pos = 0;
    do {
        const std::size_t chunk =
            std::min<std::size_t>(65535, input.size() - pos);
        const bool last = pos + chunk >= input.size();
        bw.put(last ? 1 : 0, 1);
        bw.put(0, 2);  // stored block
        bw.alignByte();
        bw.put(static_cast<std::uint32_t>(chunk), 16);
        bw.put(static_cast<std::uint32_t>((~chunk) & 0xFFFFu), 16);
        bw.out.append(input.substr(pos, chunk));
        pos += chunk;
    } while (pos < input.size());
    return std::move(bw.out);
}

}  // namespace

std::string deflateRaw(std::string_view input) {
    if (input.size() > kStoredThreshold) return deflateStored(input);
    return deflateFixed(input);
}

}  // namespace cp3::cli
