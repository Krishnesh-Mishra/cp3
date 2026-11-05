// unordered_dense_smoke.cpp -- smoke for vendored ankerl::unordered_dense
// (third_party/unordered_dense, header-only).
//
// Basic map/set behaviour the cp3 consumers rely on (std/collections Map/Set
// index, std/io fd table): insert, find (hit and miss), overwrite, erase,
// size, and iteration count. Iteration *order* is intentionally not asserted
// (unspecified), only the visited element multiset.
//
// Build (see run_smokes.ps1 / docs/design/vendor-smoke.md) -- header-only,
// no archive to link:
//   g++ -std=c++20 -O2 -I third_party/unordered_dense/include \
//       third_party/smokes/unordered_dense_smoke.cpp -o unordered_dense_smoke.exe
//
// Exit 0 on success, 1 on any mismatch. No input, no files, deterministic.

#include <cstdio>
#include <string>

#include <ankerl/unordered_dense.h>

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
}  // namespace

int main() {
    std::printf("unordered_dense smoke (map/set)\n");

    ankerl::unordered_dense::map<std::string, int> m;
    check(m.empty() && m.size() == 0, "map starts empty");
    check(m.find("missing") == m.end(), "find on empty map misses");

    m["alpha"] = 1;
    m["beta"] = 2;
    m.emplace("gamma", 3);
    check(m.size() == 3, "size after 3 inserts");

    const auto it = m.find("beta");
    check(it != m.end() && it->second == 2, "find hit returns value");

    m["alpha"] = 42;  // overwrite existing key
    check(m.size() == 3 && m.at("alpha") == 42, "overwrite keeps size, updates value");

    check(m.erase("beta") == 1, "erase existing returns 1");
    check(m.erase("beta") == 0, "erase already-erased returns 0");
    check(m.find("beta") == m.end() && m.size() == 2, "erased key gone");

    // Iteration visits exactly the remaining entries (order unspecified).
    int sum = 0;
    for (const auto& [key, value] : m) {
        (void)key;
        sum += value;
    }
    check(sum == 45, "iteration sums remaining values (42 + 3)");

    ankerl::unordered_dense::set<std::string> s;
    s.insert("one");
    s.insert("two");
    s.insert("one");  // duplicate insert is a no-op
    check(s.size() == 2, "set dedupes");
    check(s.contains("one") && !s.contains("three"), "set contains hit/miss");
    s.erase("one");
    check(!s.contains("one") && s.size() == 1, "set erase");

    // Rehash/growth path: many keys must all remain findable.
    ankerl::unordered_dense::map<int, int> big;
    for (int i = 0; i < 10000; ++i) big[i] = i * 2;
    bool all = big.size() == 10000;
    for (int i = 0; i < 10000 && all; ++i) all = big.at(i) == i * 2;
    check(all, "10k inserts all findable after growth");

    std::printf(failures ? "unordered_dense smoke: %d failure(s)\n"
                         : "unordered_dense smoke: OK\n",
                failures);
    return failures ? 1 : 0;
}
