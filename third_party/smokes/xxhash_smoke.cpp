// xxhash_smoke.cpp -- smoke for vendored XXHash (third_party/xxhash).
//
// Verifies the two canonical XXH64 (seed 0) test vectors from the task /
// upstream sanity check, plus determinism and seed sensitivity:
//
//   XXH64("",          seed 0) == 0xEF46DB3751D8E999
//   XXH64("Hello World!", seed 0) == 0x45AB6734B21E6968
//
// Build (see run_smokes.ps1 / docs/design/vendor-smoke.md):
//   g++ -std=c++20 -O2 -I third_party/xxhash/include third_party/smokes/xxhash_smoke.cpp \
//       -L third_party/xxhash/lib/windows-x64 -lxxhash -o xxhash_smoke.exe
//
// Exit 0 on success, 1 on any mismatch. No input, no files, deterministic.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "xxhash.h"

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

void check_hash(const char* label, const void* data, std::size_t len,
                std::uint64_t expected) {
    const std::uint64_t got = XXH64(data, len, 0);
    std::printf("  %s: got 0x%016llX expected 0x%016llX\n", label,
                static_cast<unsigned long long>(got),
                static_cast<unsigned long long>(expected));
    check(got == expected, label);
}
}  // namespace

int main() {
    std::printf("xxhash smoke (XXH64, seed 0)\n");

    check_hash("XXH64(\"\")", "", 0, 0xEF46DB3751D8E999ULL);
    check_hash("XXH64(\"Hello World!\")", "Hello World!", 12,
               0x45AB6734B21E6968ULL);

    // Determinism: same input, same hash, every call.
    const char* msg = "Hello World!";
    check(XXH64(msg, 12, 0) == XXH64(msg, 12, 0), "deterministic (repeat call)");

    // Seed sensitivity: a non-zero seed must change the digest. (This is a
    // sanity property, not a second pinned vector.)
    check(XXH64(msg, 12, 1) != XXH64(msg, 12, 0), "seed changes digest");

    // Streaming API must agree with the one-shot API on the same bytes.
    XXH64_state_t state;
    XXH64_reset(&state, 0);
    XXH64_update(&state, msg, 12);
    check(XXH64_digest(&state) == 0x45AB6734B21E6968ULL,
          "streaming XXH64_digest matches one-shot");

    std::printf(failures ? "xxhash smoke: %d failure(s)\n" : "xxhash smoke: OK\n",
                failures);
    return failures ? 1 : 0;
}
