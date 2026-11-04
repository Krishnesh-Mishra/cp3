// file-roundtrip (cpp entry) -- write, read and stat one fixed 4 KiB file N
// times in the OS temp directory, then remove it.
//
// Build:  g++ -std=c++20 -O3 -o file-roundtrip.exe main.cpp
// Run:    file-roundtrip.exe
//
// Same workload and same output as the cp3, Rust, Go, JavaScript and Python
// entries. The checksum folds the bytes read (length, byte sum, first and last
// byte) and the stat result (size, isFile, isDirectory), so a language that
// wrote a different file or mis-stated the metadata would print a different
// number.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

static const long long kIterations = 200;
static const std::size_t kFileBytes = 4096;
static const long long kMod = 1000003;

static std::string payload() {
    const std::string seed =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    std::string data;
    while (data.size() < kFileBytes) data += seed;
    data.resize(kFileBytes);
    return data;
}

int main() {
    const std::string data = payload();
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / "cp3-bench-file-roundtrip.txt";

    long long checksum = 0;
    for (long long i = 0; i < kIterations; ++i) {
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
            out.close();
            if (!out) {
                std::cerr << "write failed\n";
                return 1;
            }
        }

        std::ifstream in(file, std::ios::binary);
        const std::string read((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        if (!in && !in.eof()) {
            std::cerr << "read failed\n";
            return 1;
        }
        if (read.size() != kFileBytes) {
            std::cerr << "wrong file size\n";
            return 1;
        }

        const std::filesystem::file_status status = std::filesystem::status(file);
        const std::uintmax_t size = std::filesystem::file_size(file);

        long long bytes = 0;
        for (unsigned char c : read) bytes += static_cast<long long>(c);

        checksum = (checksum * 31 + static_cast<long long>(read.size())) % kMod;
        checksum = (checksum * 31 + bytes) % kMod;
        checksum = (checksum * 31 + static_cast<unsigned char>(read.front())) % kMod;
        checksum = (checksum * 31 + static_cast<unsigned char>(read.back())) % kMod;
        checksum = (checksum * 31 + static_cast<long long>(size)) % kMod;
        checksum = (checksum * 31 + (std::filesystem::is_regular_file(status) ? 1 : 0)) % kMod;
        checksum = (checksum * 31 + (std::filesystem::is_directory(status) ? 1 : 0)) % kMod;
    }

    std::filesystem::remove(file);
    std::cout << checksum << "\n";
    return 0;
}
