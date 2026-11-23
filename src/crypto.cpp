#include "crypto.h"

#include <cstdint>
#include <stdexcept>
#include <mutex>
#include <cstring>
#include <cctype>

//
// libsodium C API declarations.
//
// Only the runtime library (libsodium.so.23) is present in this environment;
// there are no development headers, so the small, stable subset of the API
// used here is declared verbatim from the libsodium headers. The constants
// below are part of libsodium's public, versioned ABI.
//

extern "C" {

int sodium_init(void);

// --- CSPRNG -----------------------------------------------------------------
void randombytes_buf(void * const buf, const size_t size);

// --- Argon2id password hashing ----------------------------------------------
// crypto_pwhash_STRBYTES = 128. Hashing uses crypto_pwhash_str_alg with the
// library's recommended interactive parameters; verification uses the
// self-describing crypto_pwhash_str_verify.
#define CRYPTO_PWHASH_STRBYTES          128
#define CRYPTO_PWHASH_ALG_ARGON2ID13    2
int crypto_pwhash_str_alg(char out[CRYPTO_PWHASH_STRBYTES],
                          const char * const passwd,
                          unsigned long long passwdlen,
                          unsigned long long opslimit, size_t memlimit, int alg);
int crypto_pwhash_str_verify(const char str[CRYPTO_PWHASH_STRBYTES],
                             const char * const passwd,
                             unsigned long long passwdlen);

// --- SHA-256 ----------------------------------------------------------------
// crypto_hash_sha256_BYTES = 32
int crypto_hash_sha256(unsigned char *out, const unsigned char *in,
                       unsigned long long inlen);

// --- Constant-time comparison ------------------------------------------------
int sodium_memcmp(const void * const b1_, const void * const b2_, size_t len);

} // extern "C"

namespace crypto {

static std::once_flag sodiumOnce;

void init() {
    std::call_once(sodiumOnce, []{
        if (sodium_init() < 0) {
            throw std::runtime_error("libsodium initialization failed");
        }
    });
}

std::string hashPassword(const std::string& password) {
    init();
    // Argon2id with libsodium's interactive limits (2 passes, 64 MiB).
    char encoded[CRYPTO_PWHASH_STRBYTES];
    std::memset(encoded, 0, sizeof(encoded));
    if (crypto_pwhash_str_alg(encoded, password.c_str(), password.size(),
                              /*opslimit*/ 2, /*memlimit*/ 64ULL << 20,
                              CRYPTO_PWHASH_ALG_ARGON2ID13) != 0) {
        throw std::runtime_error("password hashing failed");
    }
    return std::string(encoded);
}

bool isModernHash(const std::string& stored) {
    return stored.rfind("$argon2id$", 0) == 0;
}

bool verifyPassword(const std::string& password, const std::string& stored) {
    init();

    if (isModernHash(stored)) {
        char encoded[CRYPTO_PWHASH_STRBYTES];
        if (stored.size() >= CRYPTO_PWHASH_STRBYTES) return false;
        std::memset(encoded, 0, sizeof(encoded));
        std::memcpy(encoded, stored.c_str(), stored.size());
        return crypto_pwhash_str_verify(encoded, password.c_str(), password.size()) == 0;
    }

    // --- Legacy formats (migrated from the old CSV system) -------------------
    // "sha256$<salt>$<hexhash>"  — imported by the migration
    // "<salt>$<hexhash>"         — original legacy CSV format
    auto verifyLegacy = [&](const std::string& salt, const std::string& expectedHex) {
        if (salt.empty() || expectedHex.size() != 64) return false;
        std::string hex = sha256Hex(salt + password);
        // Case-insensitive compare of hex digests (legacy stored lowercase).
        if (hex.size() != expectedHex.size()) return false;
        for (size_t i = 0; i < hex.size(); ++i) {
            char a = std::tolower((unsigned char)hex[i]);
            char b = std::tolower((unsigned char)expectedHex[i]);
            if (a != b) return false;
        }
        return true;
    };

    auto first = stored.find('$');
    if (first == std::string::npos) return false;
    auto second = stored.find('$', first + 1);
    if (second == std::string::npos) {
        // "<salt>$<hash>"
        return verifyLegacy(stored.substr(0, first), stored.substr(first + 1));
    }
    if (stored.compare(0, first, "sha256") == 0) {
        // "sha256$<salt>$<hash>"
        auto third = stored.find('$', second + 1);
        if (third == std::string::npos) return false;
        return verifyLegacy(stored.substr(second + 1, third - second - 1),
                            stored.substr(third + 1));
    }
    return false;
}

std::string randomHex(size_t bytes) {
    init();
    static const char* hexDigits = "0123456789abcdef";
    std::string raw(bytes, '\0');
    randombytes_buf(raw.data(), raw.size());
    std::string out;
    out.reserve(bytes * 2);
    for (size_t i = 0; i < bytes; ++i) {
        unsigned char c = (unsigned char)raw[i];
        out.push_back(hexDigits[c >> 4]);
        out.push_back(hexDigits[c & 0x0F]);
    }
    return out;
}

std::string generateTemporaryPassword(size_t length) {
    init();
    if (length < 8) length = 16;
    // Unambiguous character subsets: omits easily confused characters (I, O, l, 0, 1)
    static const char upper[]   = "ABCDEFGHJKLMNPQRSTUVWXYZ";
    static const char lower[]   = "abcdefghijkmnopqrstuvwxyz";
    static const char digits[]  = "23456789";
    static const char symbols[] = "!@#$%*&-_=+";
    static const std::string alphabet = std::string(upper) + lower + digits + symbols;
    const size_t n = alphabet.size();
    // Rejection sampling bound to eliminate modulo bias
    const unsigned int limit = 256 - (256 % n);

    while (true) {
        std::string pwd;
        pwd.reserve(length);
        while (pwd.size() < length) {
            unsigned char b = 0;
            randombytes_buf(&b, 1);
            if (b < limit) {
                pwd.push_back(alphabet[b % n]);
            }
        }
        bool hasUpper = false, hasLower = false, hasDigit = false, hasSymbol = false;
        for (char c : pwd) {
            if (std::strchr(upper, c)) hasUpper = true;
            else if (std::strchr(lower, c)) hasLower = true;
            else if (std::strchr(digits, c)) hasDigit = true;
            else if (std::strchr(symbols, c)) hasSymbol = true;
        }
        if (hasUpper && hasLower && hasDigit && hasSymbol) {
            return pwd;
        }
    }
}

std::string sha256Hex(const std::string& input) {
    init();
    unsigned char digest[32];
    crypto_hash_sha256(digest, reinterpret_cast<const unsigned char*>(input.data()), input.size());
    static const char* hexDigits = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 32; ++i) {
        out.push_back(hexDigits[digest[i] >> 4]);
        out.push_back(hexDigits[digest[i] & 0x0F]);
    }
    return out;
}

bool constantTimeEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return sodium_memcmp(a.data(), b.data(), a.size()) == 0;
}

} // namespace crypto
