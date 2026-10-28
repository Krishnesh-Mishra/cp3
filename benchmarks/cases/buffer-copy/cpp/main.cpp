// buffer-copy (cpp entry) -- build a 64 KiB byte-pattern buffer, then reshape
// it ITERATIONS times: rotate it by a changing offset, overwrite the new first
// byte with a cycling marker, and fold a 96-byte window into a checksum.
//
// Build:  g++ -std=c++20 -O2 -o buffer-copy.exe main.cpp
// Run:    buffer-copy.exe
//
// The other five entries run the same reshape with their own buffer types
// (cp3 Buffer, Vec<u8>, []byte, Node Buffer, bytearray). Every intermediate
// buffer is exactly SIZE bytes, and every entry folds the same two marker
// positions, so all six languages must print the same number.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static const std::size_t kSize = 65536;
static const long long kIterations = 5000;
static const std::size_t kWindow = 96;
static const std::string kSeed =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!?";

int main() {
    std::vector<std::uint8_t> buf;
    buf.reserve(kSize);
    for (std::size_t i = 0; i < kSize; ++i) {
        buf.push_back(static_cast<std::uint8_t>(kSeed[i % kSeed.size()]));
    }

    long long checksum = 0;
    for (long long k = 0; k < kIterations; ++k) {
        const std::size_t offset = static_cast<std::size_t>((k * 4093 + 17) % static_cast<long long>(kSize));
        const std::uint8_t marker = static_cast<std::uint8_t>(kSeed[static_cast<std::size_t>(k) % kSeed.size()]);

        // Rotate left by offset and overwrite the new first byte with marker.
        std::vector<std::uint8_t> next;
        next.reserve(kSize);
        next.push_back(marker);
        next.insert(next.end(), buf.begin() + static_cast<std::ptrdiff_t>(offset) + 1, buf.end());
        next.insert(next.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(offset));
        buf = std::move(next);

        const auto a = std::find(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(kWindow),
                                 static_cast<std::uint8_t>('a'));
        const auto seven = std::find(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(kWindow),
                                     static_cast<std::uint8_t>('7'));
        const long long firstA = a == buf.begin() + static_cast<std::ptrdiff_t>(kWindow)
                                     ? -1
                                     : static_cast<long long>(a - buf.begin());
        const long long first7 = seven == buf.begin() + static_cast<std::ptrdiff_t>(kWindow)
                                     ? -1
                                     : static_cast<long long>(seven - buf.begin());
        checksum = (checksum * 131 + static_cast<long long>(buf.size()) +
                    (firstA + 1) + (first7 + 1)) % 1000003;
    }

    std::cout << checksum << "\n";
    return 0;
}
