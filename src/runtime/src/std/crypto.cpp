// =============================================================================
// src/runtime/src/std/crypto.cpp - `std/crypto` implementation TU.
//
// OWNERSHIP: the crypto wave. Selected (compiled + linked, with -lsodium) only
// when a program imports "std/crypto" - see src/cli/src/detail/native_std_crypto.hpp.
//
// Backend notes: libsodium 1.0.22 static (third_party/libsodium, D70). Do not
// gate AES-256-GCM on crypto_aead_aes256gcm_is_available(): in this pinned
// build it returns 0 while the round-trip works (known-answer covered by
// tests/cases/stdlib/crypto_*), so this TU never consults it.
//
// The exact surface (names, digest encodings, the AES envelope and the throw
// sites) is documented in cp3/std/crypto.hpp.
// =============================================================================
#include "cp3/std/crypto.hpp"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include <sodium.h>

#include "cp3/core/error.hpp"

namespace cp3::runtime::crypto {

namespace {

// Node's cap for crypto.randomBytes; keeps an absurd size a catchable cp3
// error instead of a std::bad_alloc (which no cp3 `catch` can take).
constexpr double kMaxRandomBytes = 2147483647.0;  // 2^31 - 1

// sodium_init is idempotent and thread-safe after one success, so running it
// on every entry point costs a load and keeps init-order out of the picture.
void ensureInit() {
    if (sodium_init() < 0) {
        throw Thrown::make(std::string("crypto: libsodium failed to initialize"));
    }
}

// A catchable cp3 error value (docs/language-spec/07): the generated
// `catch (e)` binds the boxed value this carries.
[[noreturn]] void fail(const std::string& message) {
    throw Thrown::make(message);
}

std::size_t checkedLength(double length, const char* what) {
    if (!(length >= 0.0) || length != std::floor(length) ||
        length > kMaxRandomBytes) {
        fail(std::string(what) +
             ": size must be a non-negative integer below 2^31");
    }
    return static_cast<std::size_t>(length);
}

std::string toHex(const unsigned char* data, std::size_t size) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(kDigits[data[i] >> 4]);
        out.push_back(kDigits[data[i] & 0x0f]);
    }
    return out;
}

std::string toBase64(const unsigned char* data, std::size_t size) {
    std::vector<char> text(
        sodium_base64_encoded_len(size, sodium_base64_VARIANT_ORIGINAL));
    sodium_bin2base64(text.data(), text.size(), data, size,
                      sodium_base64_VARIANT_ORIGINAL);
    return std::string(text.data());
}

std::vector<unsigned char> fromBase64(const std::string& text) {
    // A base64 text of length L decodes to at most ceil(L / 4) * 3 bytes.
    std::vector<unsigned char> bytes((text.size() / 4 + 1) * 3);
    std::size_t size = 0;
    if (sodium_base642bin(bytes.data(), bytes.size(), text.data(), text.size(),
                          nullptr, &size, nullptr,
                          sodium_base64_VARIANT_ORIGINAL) != 0) {
        fail("crypto.AES256Decrypt: ciphertext is not valid base64");
    }
    bytes.resize(size);
    return bytes;
}

// The 32-byte AES key derived from the caller's key string. BLAKE2b-256 is
// unkeyed and needs no salt: the input is the caller's whole secret, and the
// docs leave the derivation open.
void deriveKey(const std::string& key,
               unsigned char out[crypto_aead_aes256gcm_KEYBYTES]) {
    crypto_generichash(out, crypto_aead_aes256gcm_KEYBYTES,
                       reinterpret_cast<const unsigned char*>(key.data()),
                       key.size(), nullptr, 0);
}

}  // namespace

std::string randomUuid() {
    ensureInit();
    unsigned char bytes[16];
    randombytes_buf(bytes, sizeof(bytes));
    // RFC 4122 version 4 + variant 1 bits, so the output is a valid v4 UUID.
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fU) | 0x80U);

    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < sizeof(bytes); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(kDigits[bytes[i] >> 4]);
        out.push_back(kDigits[bytes[i] & 0x0f]);
    }
    return out;
}

std::string randomBytes(double length) {
    ensureInit();
    const std::size_t size = checkedLength(length, "crypto.randomBytes");
    std::string out(size, '\0');
    if (size != 0) randombytes_buf(out.data(), size);
    return out;
}

std::string sha256(const std::string& data) {
    ensureInit();
    unsigned char digest[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(digest,
                       reinterpret_cast<const unsigned char*>(data.data()),
                       data.size());
    return toHex(digest, sizeof(digest));
}

std::string sha512(const std::string& data) {
    ensureInit();
    unsigned char digest[crypto_hash_sha512_BYTES];
    crypto_hash_sha512(digest,
                       reinterpret_cast<const unsigned char*>(data.data()),
                       data.size());
    return toHex(digest, sizeof(digest));
}

std::string aes256Encrypt(const std::string& key, const std::string& data) {
    ensureInit();
    unsigned char k[crypto_aead_aes256gcm_KEYBYTES];
    deriveKey(key, k);

    unsigned char nonce[crypto_aead_aes256gcm_NPUBBYTES];
    randombytes_buf(nonce, sizeof(nonce));

    std::vector<unsigned char> combined(data.size() +
                                        crypto_aead_aes256gcm_ABYTES);
    unsigned long long clen = 0;
    if (crypto_aead_aes256gcm_encrypt(
            combined.data(), &clen,
            reinterpret_cast<const unsigned char*>(data.data()), data.size(),
            nullptr, 0, nullptr, nonce, k) != 0) {
        fail("crypto.AES256: encryption failed");
    }
    combined.resize(static_cast<std::size_t>(clen));

    std::vector<unsigned char> envelope;
    envelope.reserve(sizeof(nonce) + combined.size());
    envelope.insert(envelope.end(), nonce, nonce + sizeof(nonce));
    envelope.insert(envelope.end(), combined.begin(), combined.end());
    return toBase64(envelope.data(), envelope.size());
}

std::string aes256Decrypt(const std::string& key, const std::string& encrypted) {
    ensureInit();
    const std::vector<unsigned char> envelope = fromBase64(encrypted);
    constexpr std::size_t kNonceBytes = crypto_aead_aes256gcm_NPUBBYTES;
    constexpr std::size_t kTagBytes = crypto_aead_aes256gcm_ABYTES;
    if (envelope.size() < kNonceBytes + kTagBytes) {
        fail("crypto.AES256Decrypt: ciphertext envelope is too short");
    }
    unsigned char k[crypto_aead_aes256gcm_KEYBYTES];
    deriveKey(key, k);

    const unsigned char* nonce = envelope.data();
    const unsigned char* cipher = envelope.data() + kNonceBytes;
    const unsigned long long clen =
        static_cast<unsigned long long>(envelope.size() - kNonceBytes);
    // At least one byte keeps `data()` non-null for a zero-length message.
    std::vector<unsigned char> message(
        static_cast<std::size_t>(clen) - kTagBytes + 1);
    unsigned long long mlen = 0;
    if (crypto_aead_aes256gcm_decrypt(message.data(), &mlen, nullptr, cipher,
                                      clen, nullptr, 0, nonce, k) != 0) {
        fail("crypto.AES256Decrypt: authentication failed (wrong key or "
             "tampered ciphertext)");
    }
    return std::string(reinterpret_cast<const char*>(message.data()),
                       static_cast<std::size_t>(mlen));
}

}  // namespace cp3::runtime::crypto
