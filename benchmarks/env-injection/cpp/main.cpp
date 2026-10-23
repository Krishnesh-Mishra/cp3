// env-injection (cpp entry) -- read one injected environment variable N times
// and fold its bytes into a checksum.
//
// Build:  g++ -std=c++20 -O2 -o env-injection.exe main.cpp
// Run:    env-injection.exe        (CP3_BENCH_ENV must be set)
//
// The suite runner injects CP3_BENCH_ENV into every child process for this
// task. Each iteration performs a fresh getenv read (the lookup itself is the
// workload) and folds the value's length, byte sum, first byte and last byte
// into the checksum. A missing variable is an error, so a run without the
// injection fails instead of measuring an empty string.
//
// Same workload and same output as the cp3, Rust, Go, JavaScript and Python
// entries.

#include <cstdlib>
#include <iostream>
#include <string>

static const long long kIterations = 500000;
static const char* kKey = "CP3_BENCH_ENV";
static const long long kMod = 1000003;

int main() {
    const char* raw = std::getenv(kKey);
    if (raw == nullptr) {
        std::cerr << "CP3_BENCH_ENV is not set\n";
        return 1;
    }

    long long checksum = 0;
    for (long long i = 0; i < kIterations; ++i) {
        const char* current = std::getenv(kKey);
        if (current == nullptr) {
            std::cerr << "CP3_BENCH_ENV is not set\n";
            return 1;
        }
        const std::string value(current);
        long long bytes = 0;
        for (unsigned char c : value) {
            bytes += static_cast<long long>(c);
        }
        checksum = (checksum * 31 + static_cast<long long>(value.size())) % kMod;
        checksum = (checksum * 31 + bytes) % kMod;
        checksum = (checksum * 31 + static_cast<unsigned char>(value.front())) % kMod;
        checksum = (checksum * 31 + static_cast<unsigned char>(value.back())) % kMod;
    }
    std::cout << checksum << "\n";
    return 0;
}
