// path-join (cpp entry) -- join, normalize, basename and extname one fixed
// path-segment list N times and fold stable string lengths and characters.
//
// Build:  g++ -std=c++20 -O2 -o path-join.exe main.cpp
// Run:    path-join.exe
//
// Same workload and same output as the cp3, Rust, Go, JavaScript and Python
// entries. Only lengths and the first character of each result are folded, so
// the checksum is identical on Windows and POSIX (the separator is one
// character in every language's path surface).

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>

static const long long kIterations = 170000;
static const long long kMod = 1000003;

int main() {
    const char* parts[] = {"var", "log", "cp3", "service", "events-2026-10-23.log"};

    long long checksum = 0;
    for (long long i = 0; i < kIterations; ++i) {
        std::filesystem::path joined;
        for (const char* part : parts) joined /= part;
        const std::filesystem::path normalized = joined.lexically_normal();

        const std::string joined_text = joined.string();
        const std::string normalized_text = normalized.string();
        const std::string base = joined.filename().string();
        const std::string ext = joined.extension().string();

        checksum = (checksum * 31 + static_cast<long long>(joined_text.size())) % kMod;
        checksum = (checksum * 31 + static_cast<long long>(normalized_text.size())) % kMod;
        checksum = (checksum * 31 + static_cast<long long>(base.size())) % kMod;
        checksum = (checksum * 31 + static_cast<long long>(ext.size())) % kMod;
        checksum = (checksum * 31 + static_cast<unsigned char>(joined_text[0])) % kMod;
        checksum = (checksum * 31 + static_cast<unsigned char>(base[0])) % kMod;
        checksum = (checksum * 31 + static_cast<unsigned char>(ext[0])) % kMod;
    }
    std::cout << checksum << "\n";
    return 0;
}
