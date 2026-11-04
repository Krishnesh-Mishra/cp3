// file-big (cpp entry) -- write and read back an 8 MiB binary payload in the
// OS temp directory 8 times, folding the read length, byte sum, first byte and
// last byte each round.
//
// Build:  g++ -std=c++20 -O3 -o file-big.exe main.cpp
// Run:    file-big.exe
//
// Same workload and same output in every language entry: 779913.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static const std::size_t kSize = 8388608;
static const long long kIterations = 8;
static const long long kMod = 1000003;

int main() {
    const std::string seed =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    std::string payload;
    payload.reserve(kSize);
    for (std::size_t i = 0; i < kSize / seed.size(); ++i) {
        payload += seed;
    }

    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "cp3-bench-file-big-cpp.bin";

    long long checksum = 0;
    for (long long iteration = 0; iteration < kIterations; ++iteration) {
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(payload.data(),
                      static_cast<std::streamsize>(payload.size()));
            out.close();
            if (!out) return 1;
        }

        std::vector<unsigned char> read(kSize);
        {
            std::ifstream in(file, std::ios::binary);
            in.read(reinterpret_cast<char*>(read.data()),
                    static_cast<std::streamsize>(read.size()));
            if (in.gcount() != static_cast<std::streamsize>(read.size())) {
                return 1;
            }
        }

        long long byteSum = 0;
        for (unsigned char b : read) {
            byteSum += static_cast<long long>(b);
        }

        checksum = (checksum * 31 + static_cast<long long>(read.size())) % kMod;
        checksum = (checksum * 31 + byteSum) % kMod;
        checksum = (checksum * 31 + static_cast<long long>(read.front())) % kMod;
        checksum = (checksum * 31 + static_cast<long long>(read.back())) % kMod;
    }

    std::filesystem::remove(file);
    std::cout << checksum << "\n";
    return 0;
}
