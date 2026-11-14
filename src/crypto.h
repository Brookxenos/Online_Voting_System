#ifndef CRYPTO_H
#define CRYPTO_H

#include <string>
#include <cstdint>
#include <vector>

//
// Crypto module — provides:
//   1. SHA-256 password hashing (FIPS 180-4 compliant, public-domain impl)
//   2. A simple XOR-stream cipher for vote payload encryption between
//      client and server (educational, not for production use).
//
namespace crypto {

// Returns a 64-char lowercase hex SHA-256 digest of the given input.
std::string sha256(const std::string& input);

// Convenience: hash a password with a per-record salt.
// Returns "<salt>$<sha256(salt + password)>".
std::string hashPassword(const std::string& password, const std::string& salt);

// Verify a password against a stored "salt$hash" string.
bool verifyPassword(const std::string& password, const std::string& stored);

// Generate a random hex salt of the given byte length (default 16 bytes -> 32 hex chars).
std::string generateSalt(size_t bytes = 16);

// XOR-stream encrypt/decrypt (symmetric). Same function for both directions.
// key should be at least 16 chars for reasonable mixing.
std::string xorCrypt(const std::string& data, const std::string& key);

// Base64 encode / decode (used for transporting encrypted binary blobs in JSON).
std::string base64Encode(const std::string& data);
std::string base64Decode(const std::string& encoded);

} // namespace crypto

#endif // CRYPTO_H
