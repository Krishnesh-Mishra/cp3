// os-probe (cpp entry) -- query platform, CPU count, home, temp and hostname
// in a tight loop, folding only machine-independent booleans.
//
// Build:  g++ -std=c++20 -O2 -o os-probe.exe main.cpp
// Run:    os-probe.exe
//
// Same workload, same fold and same output in every language entry. The
// platform name is mapped to 1/2/3 and reduced to a "documented platform"
// boolean; the CPU count and the three path/name strings reduce to
// non-empty/positive booleans. No path, hostname or core count is folded, so
// the checksum cannot vary between machines.

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr long long kIterations = 50000;
constexpr long long kMod = 1000003;

int platformCode() {
#if defined(_WIN32)
    return 1;  // "windows"
#elif defined(__APPLE__)
    return 2;  // "macos"
#elif defined(__linux__)
    return 3;  // "linux"
#else
    return 0;
#endif
}

std::string env(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

std::string firstEnv(const char* first, const char* second) {
    const std::string value = env(first);
    return value.empty() ? env(second) : value;
}

}  // namespace

int main() {
    long long checksum = 0;

    for (long long k = 0; k < kIterations; ++k) {
        const int code = platformCode();
        const int platformOk = (code >= 1 && code <= 3) ? 1 : 0;
        const int cpuOk = std::thread::hardware_concurrency() > 0 ? 1 : 0;

#if defined(_WIN32)
        const std::string home = firstEnv("USERPROFILE", "HOME");
        const std::string temp = firstEnv("TEMP", "TMP");
        const std::string host = firstEnv("COMPUTERNAME", "HOSTNAME");
#else
        const std::string home = env("HOME");
        const std::string temp = firstEnv("TMPDIR", "TMP");
        const std::string host = env("HOSTNAME");
#endif

        const int homeOk = home.empty() ? 0 : 1;
        const int tempOk = temp.empty() ? 0 : 1;
        const int hostOk = host.empty() ? 0 : 1;

        const int term = platformOk * 16 + cpuOk * 8 + homeOk * 4 +
                         tempOk * 2 + hostOk;
        checksum = (checksum * 31 + term) % kMod;
    }

    std::cout << checksum << "\n";
    return 0;
}
