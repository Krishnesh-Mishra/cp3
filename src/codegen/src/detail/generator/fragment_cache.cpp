// =============================================================================
// src/codegen/src/detail/fragment_cache.cpp - PRIVATE per-unit codegen
// fragment cache DEFINITIONS.
//
// OWNERSHIP: codegen module (src/codegen/**).
// Format documented in detail/generator/fragment_cache.hpp. The CLI rewrites
// the file with only the records whose full key matches; this side re-verifies
// the source hash at splice time and rewrites the file with the union of
// reused and freshly emitted records.
// =============================================================================
#include "detail/generator/fragment_cache.hpp"

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>

namespace cp3::detail {

namespace {

void putStr(std::string& out, const std::string& text) {
    out += std::to_string(text.size());
    out += ':';
    out += text;
}

bool takeStr(const std::string& in, std::size_t& at, std::string& out) {
    const std::size_t sep = in.find(':', at);
    if (sep == std::string::npos) return false;
    std::size_t len = 0;
    try {
        len = std::stoul(in.substr(at, sep - at));
    } catch (const std::exception&) {
        return false;
    }
    if (sep + 1 + len > in.size()) return false;
    out = in.substr(sep + 1, len);
    at = sep + 1 + len;
    return true;
}

std::vector<std::string> splitNewline(const std::string& text) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) {
            out.push_back(text.substr(at));
            break;
        }
        out.push_back(text.substr(at, nl - at));
        at = nl + 1;
    }
    return out;
}

std::string joinNewline(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += item;
        out += '\n';
    }
    return out;
}

std::string hexEncode(const std::string& text) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(text.size() * 2);
    for (unsigned char c : text) {
        out.push_back(kDigits[c >> 4]);
        out.push_back(kDigits[c & 0xF]);
    }
    return out;
}

bool hexDecode(const std::string& text, std::string& out) {
    out.clear();
    if (text.size() % 2 != 0) return false;
    out.reserve(text.size() / 2);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        const int hi = nibble(text[i]);
        const int lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

std::uint64_t fnv1a(const std::string& text) {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

}  // namespace

std::uint64_t fragmentSourceHashOf(const CompilationUnit& unit) {
    if (unit.file == nullptr) return 0;
    return fnv1a(unit.file->text);
}

FragmentCache* loadFragmentCache() {
    const char* env = std::getenv("CP3_INCREMENTAL_FRAGMENTS");
    if (env == nullptr || *env == '\0') return nullptr;
    std::ifstream in(env);
    if (!in) return nullptr;
    std::string line;
    if (!std::getline(in, line)) return nullptr;
    if (line != "cp3-incremental-cache/3") return nullptr;
    auto cache = std::make_unique<FragmentCache>();
    while (std::getline(in, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::size_t at = 0;
        FragmentRecord rec;
        std::string needs, headers, flags;
        if (!takeStr(line, at, rec.unitKey)) continue;
        if (!takeStr(line, at, rec.sourceHash)) continue;
        if (!takeStr(line, at, rec.ihFingerprint)) continue;
        if (!takeStr(line, at, rec.shapeHash)) continue;
        if (!takeStr(line, at, rec.configHash)) continue;
        if (!takeStr(line, at, rec.interfaceHash)) continue;
        if (!takeStr(line, at, rec.fragmentSourceKey)) continue;
        if (!takeStr(line, at, rec.namesp)) continue;
        std::string raw;
        if (!takeStr(line, at, raw)) continue;
        if (!hexDecode(raw, rec.globals)) continue;
        if (!takeStr(line, at, raw)) continue;
        if (!hexDecode(raw, rec.decls)) continue;
        if (!takeStr(line, at, raw)) continue;
        if (!hexDecode(raw, rec.defs)) continue;
        if (!takeStr(line, at, raw)) continue;
        if (!hexDecode(raw, rec.mainBody)) continue;
        if (!takeStr(line, at, needs)) continue;
        if (!takeStr(line, at, headers)) continue;
        if (!takeStr(line, at, flags)) continue;
        rec.needsMask = 0;
        bool needsOk = true;
        std::stringstream bits(needs);
        std::string bit;
        while (bits >> bit) {
            try {
                rec.needsMask |= (std::uint64_t{1} << std::stoul(bit));
            } catch (const std::exception&) {
                needsOk = false;
                break;
            }
        }
        if (!needsOk) continue;  // drop malformed records entirely
        std::string decodedHeaders;
        if (!hexDecode(headers, decodedHeaders)) continue;
        rec.nativeHeaders = splitNewline(decodedHeaders);
        unsigned b = 0;
        try {
            b = static_cast<unsigned>(std::stoul(flags));
        } catch (const std::exception&) {
            continue;
        }
        rec.isEntry = (b & 1u) != 0;
        rec.isPackage = (b & 2u) != 0;
        rec.topLevelAwait = (b & 4u) != 0;
        cache->records[rec.unitKey] = std::move(rec);
    }
    return cache.release();
}

bool writeFragmentCache(const std::filesystem::path& cachePath,
                        const std::vector<FragmentRecord>& records) {
    std::string out;
    out += "cp3-incremental-cache/3";
    out += '\n';
    for (const FragmentRecord& rec : records) {
        std::string line;
        putStr(line, rec.unitKey);
        putStr(line, rec.sourceHash);
        putStr(line, rec.ihFingerprint);
        putStr(line, rec.shapeHash);
        putStr(line, rec.configHash);
        putStr(line, rec.interfaceHash);
        putStr(line, rec.fragmentSourceKey);
        putStr(line, rec.namesp);
        putStr(line, hexEncode(rec.globals));
        putStr(line, hexEncode(rec.decls));
        putStr(line, hexEncode(rec.defs));
        putStr(line, hexEncode(rec.mainBody));
        std::string needs;
        for (int bit = 0; bit < 64; ++bit) {
            if (rec.needsMask & (std::uint64_t{1} << bit)) {
                if (!needs.empty()) needs += ' ';
                needs += std::to_string(bit);
            }
        }
        putStr(line, needs);
        putStr(line, hexEncode(joinNewline(rec.nativeHeaders)));
        unsigned bits = 0;
        if (rec.isEntry) bits |= 1u;
        if (rec.isPackage) bits |= 2u;
        if (rec.topLevelAwait) bits |= 4u;
        putStr(line, std::to_string(bits));
        out += line;
        out += '\n';
    }
    const std::filesystem::path tmp =
        std::filesystem::path(cachePath).string() + ".tmp";
    {
        std::ofstream outFile(tmp, std::ios::binary | std::ios::trunc);
        if (!outFile) return false;
        outFile << out;
        if (!outFile) return false;
    }
    std::error_code rmEc;
    std::filesystem::remove(cachePath, rmEc);  // best effort; cache is not blocking
    std::error_code ec;
    std::filesystem::rename(tmp, cachePath, ec);
    if (ec) {
        std::error_code rmEc2;
        std::filesystem::remove(tmp, rmEc2);
        return false;
    }
    return true;
}

}  // namespace cp3::detail
