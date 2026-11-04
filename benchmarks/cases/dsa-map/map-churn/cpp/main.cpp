// map-churn (cpp entry) -- fill a std::unordered_map with 100,000 integer
// keys, then run two rounds of delete / reinsert / in-place update churn and
// fold every key in ascending order into one integer.
//
// Build:  g++ -std=c++20 -O3 -o map-churn.exe main.cpp
// Run:    map-churn.exe
//
// Same workload and same output in every language entry: 942471769.

#include <iostream>
#include <unordered_map>

static const long long kKeys = 100000;
static const long long kRounds = 2;
static const long long kMod = 1000000007;

int main() {
    std::unordered_map<long long, long long> map;
    for (long long i = 0; i < kKeys; ++i) {
        map[i] = i * 3 + 1;
    }

    long long checksum = 0;
    for (long long r = 0; r < kRounds; ++r) {
        for (long long i = 0; i < kKeys; i += 10) {
            map.erase(i);
        }
        for (long long i = 0; i < kKeys; i += 10) {
            map[i] = i * 5 + r + 2;
        }
        for (long long i = 0; i < kKeys; i += 7) {
            map[i] = map[i] + 1;
        }
        for (long long i = 0; i < kKeys; ++i) {
            checksum = (checksum * 31 + map[i]) % kMod;
        }
    }

    std::cout << checksum << "\n";
    return 0;
}
