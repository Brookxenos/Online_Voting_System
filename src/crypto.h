#ifndef CRYPTO_H
#define CRYPTO_H

//
// Crypto module — security-sensitive helpers built on libsodium
// (a maintained, independently reviewed library). Nothing here invents
// cryptographic primitives.
//
//   - Argon2id password hashing (crypto_pwhash_str / crypto_pwhash_str_verify)
//   - CSPRNG (libsodium randombytes_buf) for salts, session tokens, nonces
//   - SHA-256 hashing (libsodium crypto_hash_sha256) for token hashing and
//     ballot receipts
//   - Legacy "salt$sha256hex" password verification for data migrated from
//     the old CSV system (verification only; new/updated hashes are Argon2id)
//

#include <string>

namespace crypto {

// Initialize libsodium. Throws std::runtime_error on failure.
void init();

// --- Password hashing (Argon2id via libsodium's encoded-string API) --------

// Hash a password. Returns an encoded PHC-format string ("$argon2id$...").
std::string hashPassword(const std::string& password);

// Verify a password against a stored hash. Supports:
//   - Argon2id encoded strings (current format)
//   - legacy "sha256$salt$hexhash" format (migrated CSV accounts)
//   - legacy raw "salt$hexhash" format (old CSV accounts, pre-migration)
bool verifyPassword(const std::string& password, const std::string& stored);

// True if the stored hash uses the current Argon2id format. Legacy-format
// hashes should be upgraded to Argon2id after a successful verification.
bool isModernHash(const std::string& stored);

// --- Cryptographically secure randomness -----------------------------------

// Hex string of `bytes` random bytes from the OS CSPRNG (via libsodium).
std::string randomHex(size_t bytes = 32);

// --- Hashing ----------------------------------------------------------------

// Lowercase hex SHA-256 digest (libsodium implementation).
std::string sha256Hex(const std::string& input);

// Constant-time string comparison for secret material.
bool constantTimeEquals(const std::string& a, const std::string& b);

} // namespace crypto

#endif // CRYPTO_H
