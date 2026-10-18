// =============================================================================
// src/cli/src/archive/inflate.cpp - raw DEFLATE decompressor DEFINITIONS (RFC 1951).
//
// OWNERSHIP: cli module (src/cli/**). Implements cli::inflateRaw
// (detail/deflate.hpp).
//
// Supports every block type - stored, fixed Huffman and dynamic Huffman - so
// a `.cp3pak` produced by any conforming gzip tool can be read, not just the
// one this CLI writes. Decoding follows the canonical "counts + ordered
// symbols" Huffman scheme; no lookup tables beyond the spec's own length and
// distance tables.
// =============================================================================
#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "detail/deflate.hpp"
#include "detail/deflate_tables.hpp"

namespace cp3::cli {

namespace {

// Guard against a hostile stream claiming an enormous expansion.
constexpr std::size_t kMaxOutput = 512u * 1024u * 1024u;

bool fail(std::string& err, const char* message) {
    err = message;
    return false;
}

// LSB-first bit reader over a byte range.
class BitReader {
public:
    BitReader(const unsigned char* data, std::size_t size)
        : data_(data), size_(size) {}

    bool take(int n, std::uint32_t& out) {
        if (!ensure(n)) return false;
        out = buf_ & ((1u << n) - 1u);
        buf_ >>= n;
        bits_ -= n;
        return true;
    }

    // Skips to the next byte boundary; returns the number of input bytes
    // consumed by the stream so far.
    std::size_t alignByte() {
        const int drop = bits_ & 7;
        buf_ >>= drop;
        bits_ -= drop;
        return pos_ - static_cast<std::size_t>(bits_ / 8);
    }

private:
    bool ensure(int n) {
        while (bits_ < n) {
            if (pos_ >= size_) return false;
            buf_ |= static_cast<std::uint32_t>(data_[pos_++]) << bits_;
            bits_ += 8;
        }
        return true;
    }

    const unsigned char* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    std::uint32_t buf_ = 0;
    int bits_ = 0;
};

// Canonical Huffman decoder built from code lengths.
struct Huffman {
    std::array<std::uint16_t, 16> count{};
    std::vector<std::uint16_t> symbols;

    bool build(const std::uint8_t* lengths, std::size_t n) {
        count.fill(0);
        for (std::size_t i = 0; i < n; ++i) {
            if (lengths[i] > 15) return false;
            ++count[lengths[i]];
        }
        count[0] = 0;
        // Kraft inequality: reject an over-subscribed code set.
        std::uint32_t left = 1;
        for (int len = 1; len <= 15; ++len) {
            left <<= 1;
            if (count[len] > left) return false;
            left -= count[len];
        }
        std::array<std::uint32_t, 16> fill{};
        std::uint32_t total = 0;
        for (int len = 1; len <= 15; ++len) {
            fill[len] = total;
            total += count[len];
        }
        symbols.assign(total, 0);
        for (std::size_t i = 0; i < n; ++i) {
            if (lengths[i] != 0) symbols[fill[lengths[i]]++] = i;
        }
        return true;
    }

    // Canonical decode; returns -1 on an invalid code.
    int decode(BitReader& br) const {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= 15; ++len) {
            std::uint32_t bit = 0;
            if (!br.take(1, bit)) return -1;
            code |= static_cast<int>(bit);
            const int cnt = count[len];
            if (code - cnt < first) {
                const auto slot = static_cast<std::size_t>(index + code - first);
                return symbols[slot];
            }
            index += cnt;
            first = (first + cnt) << 1;
            code <<= 1;
        }
        return -1;
    }
};

void buildFixed(Huffman& lit, Huffman& dist) {
    std::array<std::uint8_t, 288> litLens{};
    for (int i = 0; i <= 143; ++i) litLens[i] = 8;
    for (int i = 144; i <= 255; ++i) litLens[i] = 9;
    for (int i = 256; i <= 279; ++i) litLens[i] = 7;
    for (int i = 280; i <= 287; ++i) litLens[i] = 8;
    lit.build(litLens.data(), litLens.size());

    std::array<std::uint8_t, 30> distLens{};
    distLens.fill(5);
    dist.build(distLens.data(), distLens.size());
}

bool inflateStored(BitReader& br, std::string& out, std::string& err) {
    (void)br.alignByte();
    std::uint32_t len = 0;
    std::uint32_t nlen = 0;
    if (!br.take(16, len) || !br.take(16, nlen)) {
        return fail(err, "truncated stored block header");
    }
    if ((len ^ 0xFFFFu) != nlen) return fail(err, "stored block length mismatch");
    for (std::uint32_t i = 0; i < len; ++i) {
        std::uint32_t byte = 0;
        if (!br.take(8, byte)) return fail(err, "truncated stored block");
        out.push_back(static_cast<char>(byte));
    }
    if (out.size() > kMaxOutput) return fail(err, "inflated data too large");
    return true;
}

bool appendMatch(std::string& out, std::uint32_t length, std::uint32_t distance,
                 std::string& err) {
    if (distance == 0 || distance > out.size()) {
        return fail(err, "invalid back-reference distance");
    }
    const std::size_t src = out.size() - distance;
    for (std::uint32_t i = 0; i < length; ++i) {
        out.push_back(out[src + i]);
    }
    return true;
}

bool inflateSymbols(BitReader& br, std::string& out, const Huffman& lit,
                    const Huffman& dist, std::string& err) {
    for (;;) {
        const int sym = lit.decode(br);
        if (sym < 0) return fail(err, "invalid literal/length code");
        if (sym < 256) {
            out.push_back(static_cast<char>(sym));
            if (out.size() > kMaxOutput) {
                return fail(err, "inflated data too large");
            }
            continue;
        }
        if (sym == 256) return true;
        const int lenIndex = sym - 257;
        if (lenIndex >= 29) return fail(err, "invalid length code");
        std::uint32_t extra = 0;
        if (!br.take(kLenExtra[lenIndex], extra)) {
            return fail(err, "truncated length extra bits");
        }
        const std::uint32_t length = kLenBase[lenIndex] + extra;
        const int distSym = dist.decode(br);
        if (distSym < 0 || distSym >= 30) return fail(err, "invalid distance code");
        if (!br.take(kDistExtra[distSym], extra)) {
            return fail(err, "truncated distance extra bits");
        }
        const std::uint32_t distance = kDistBase[distSym] + extra;
        if (!appendMatch(out, length, distance, err)) return false;
        if (out.size() > kMaxOutput) return fail(err, "inflated data too large");
    }
}

void readCodeLengths(BitReader& br, const Huffman& clen,
                     std::vector<std::uint8_t>& lengths, std::string& err) {
    std::size_t i = 0;
    while (i < lengths.size()) {
        const int sym = clen.decode(br);
        if (sym < 0) {
            err = "invalid code-length code";
            return;
        }
        if (sym < 16) {
            lengths[i++] = static_cast<std::uint8_t>(sym);
            continue;
        }
        std::uint32_t extra = 0;
        const int count = sym == 16 ? 3 : sym == 17 ? 3 : 11;
        const int bits = sym == 16 ? 2 : sym == 17 ? 3 : 7;
        if (sym == 16) {
            if (i == 0) {
                err = "repeat with no previous code length";
                return;
            }
            if (!br.take(bits, extra)) {
                err = "truncated code-length repeat";
                return;
            }
            for (int n = 0; n < count + static_cast<int>(extra) && i < lengths.size();
                 ++n) {
                lengths[i] = lengths[i - 1];
                ++i;
            }
            continue;
        }
        if (!br.take(bits, extra)) {
            err = "truncated code-length repeat";
            return;
        }
        const std::size_t repeat =
            static_cast<std::size_t>(count + static_cast<int>(extra));
        for (std::size_t n = 0; n < repeat && i < lengths.size(); ++n) {
            lengths[i++] = 0;
        }
    }
}

bool inflateDynamic(BitReader& br, std::string& out, std::string& err) {
    std::uint32_t hlit = 0;
    std::uint32_t hdist = 0;
    std::uint32_t hclen = 0;
    if (!br.take(5, hlit) || !br.take(5, hdist) || !br.take(4, hclen)) {
        return fail(err, "truncated dynamic block header");
    }
    const std::size_t litCount = 257 + hlit;
    const std::size_t distCount = 1 + hdist;
    static constexpr int kOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                       11, 4,  12, 3, 13, 2, 14, 1, 15};
    std::array<std::uint8_t, 19> clenLens{};
    for (std::size_t i = 0; i < 4 + hclen; ++i) {
        std::uint32_t bits = 0;
        if (!br.take(3, bits)) return fail(err, "truncated code-length lengths");
        clenLens[kOrder[i]] = static_cast<std::uint8_t>(bits);
    }
    Huffman clen;
    if (!clen.build(clenLens.data(), clenLens.size())) {
        return fail(err, "invalid code-length code lengths");
    }

    std::vector<std::uint8_t> lengths(litCount + distCount, 0);
    readCodeLengths(br, clen, lengths, err);
    if (!err.empty()) return false;

    Huffman lit;
    Huffman dist;
    if (!lit.build(lengths.data(), litCount)) {
        return fail(err, "invalid literal/length code lengths");
    }
    if (!dist.build(lengths.data() + litCount, distCount)) {
        return fail(err, "invalid distance code lengths");
    }
    return inflateSymbols(br, out, lit, dist, err);
}

}  // namespace

bool inflateRaw(std::string_view input, std::string& out, std::size_t& consumed,
                std::string& err) {
    err.clear();
    out.clear();
    consumed = 0;
    BitReader br(reinterpret_cast<const unsigned char*>(input.data()),
                 input.size());
    bool final = false;
    while (!final) {
        std::uint32_t last = 0;
        std::uint32_t type = 0;
        if (!br.take(1, last) || !br.take(2, type)) {
            return fail(err, "truncated deflate stream");
        }
        final = last != 0;
        if (type == 0) {
            if (!inflateStored(br, out, err)) return false;
        } else if (type == 1) {
            Huffman lit;
            Huffman dist;
            buildFixed(lit, dist);
            if (!inflateSymbols(br, out, lit, dist, err)) return false;
        } else if (type == 2) {
            if (!inflateDynamic(br, out, err)) return false;
        } else {
            return fail(err, "reserved deflate block type");
        }
    }
    consumed = br.alignByte();
    return true;
}

}  // namespace cp3::cli
