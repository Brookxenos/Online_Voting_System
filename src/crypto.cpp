#include "crypto.h"

#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>
#include <stdexcept>

namespace crypto {

// ---------------------------------------------------------------------------
// SHA-256 implementation (FIPS 180-4). Public-domain, no external deps.
// ---------------------------------------------------------------------------

namespace {

constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

void sha256Transform(const uint8_t block[64], uint32_t state[8]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(block[i*4]) << 24) |
               (uint32_t(block[i*4+1]) << 16) |
               (uint32_t(block[i*4+2]) << 8)  |
               (uint32_t(block[i*4+3]));
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=state[0],b=state[1],c=state[2],d=state[3];
    uint32_t e=state[4],f=state[5],g=state[6],h=state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

} // anonymous namespace

std::string sha256(const std::string& input) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    const uint8_t* data = reinterpret_cast<const uint8_t*>(input.data());
    uint64_t bitLen = input.size() * 8ULL;
    size_t fullBlocks = input.size() / 64;
    for (size_t i = 0; i < fullBlocks; ++i) {
        sha256Transform(data + i*64, state);
    }
    // Final block(s) with padding.
    uint8_t last[128] = {0};
    size_t rem = input.size() - fullBlocks * 64;
    for (size_t i = 0; i < rem; ++i) last[i] = data[fullBlocks*64 + i];
    last[rem] = 0x80;
    size_t totalLen = rem + 1;
    if (totalLen > 56) {
        // need two blocks
        for (int i = 0; i < 8; ++i)
            last[56 + i] = uint8_t((bitLen >> (56 - 8*i)) & 0xff);
        sha256Transform(last, state);
        uint8_t second[64] = {0};
        for (int i = 0; i < 8; ++i)
            second[56 + i] = uint8_t((bitLen >> (56 - 8*i)) & 0xff);
        sha256Transform(second, state);
    } else {
        for (int i = 0; i < 8; ++i)
            last[56 + i] = uint8_t((bitLen >> (56 - 8*i)) & 0xff);
        sha256Transform(last, state);
    }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i) oss << std::setw(8) << state[i];
    return oss.str();
}

std::string hashPassword(const std::string& password, const std::string& salt) {
    return salt + "$" + sha256(salt + password);
}

bool verifyPassword(const std::string& password, const std::string& stored) {
    auto pos = stored.find('$');
    if (pos == std::string::npos) return false;
    std::string salt = stored.substr(0, pos);
    std::string hash = stored.substr(pos + 1);
    return sha256(salt + password) == hash;
}

std::string generateSalt(size_t bytes) {
    static std::mt19937_64 rng(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < bytes; ++i) {
        oss << std::setw(2) << (uint32_t)(rng() & 0xff);
    }
    return oss.str();
}

// ---------------------------------------------------------------------------
// XOR-stream cipher (educational). Symmetric — same function encrypts/decrypts.
// ---------------------------------------------------------------------------

std::string xorCrypt(const std::string& data, const std::string& key) {
    if (key.empty()) return data;
    std::string out = data;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<char>(out[i] ^ key[i % key.size()]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Base64
// ---------------------------------------------------------------------------

namespace {
const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int b64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
}

std::string base64Encode(const std::string& data) {
    std::string out;
    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t v = (uint8_t)data[i] << 16 | (uint8_t)data[i+1] << 8 | (uint8_t)data[i+2];
        out.push_back(B64[(v >> 18) & 0x3f]);
        out.push_back(B64[(v >> 12) & 0x3f]);
        out.push_back(B64[(v >> 6) & 0x3f]);
        out.push_back(B64[v & 0x3f]);
        i += 3;
    }
    size_t rem = data.size() - i;
    if (rem == 1) {
        uint32_t v = (uint8_t)data[i] << 16;
        out.push_back(B64[(v >> 18) & 0x3f]);
        out.push_back(B64[(v >> 12) & 0x3f]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        uint32_t v = (uint8_t)data[i] << 16 | (uint8_t)data[i+1] << 8;
        out.push_back(B64[(v >> 18) & 0x3f]);
        out.push_back(B64[(v >> 12) & 0x3f]);
        out.push_back(B64[(v >> 6) & 0x3f]);
        out.push_back('=');
    }
    return out;
}

std::string base64Decode(const std::string& encoded) {
    std::string out;
    std::vector<int> vals;
    for (char c : encoded) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = b64Val(c);
        if (v < 0) continue;
        vals.push_back(v);
    }
    for (size_t i = 0; i + 3 < vals.size() + 1; i += 4) {
        if (i + 4 > vals.size()) break;
        uint32_t v = (uint32_t(vals[i]) << 18) | (uint32_t(vals[i+1]) << 12) |
                     (uint32_t(vals[i+2]) << 6)  | uint32_t(vals[i+3]);
        out.push_back(char((v >> 16) & 0xff));
        out.push_back(char((v >> 8) & 0xff));
        out.push_back(char(v & 0xff));
    }
    size_t rem = vals.size() % 4;
    if (rem == 2) {
        uint32_t v = (uint32_t(vals[vals.size()-2]) << 18) | (uint32_t(vals[vals.size()-1]) << 12);
        out.push_back(char((v >> 16) & 0xff));
    } else if (rem == 3) {
        uint32_t v = (uint32_t(vals[vals.size()-3]) << 18) | (uint32_t(vals[vals.size()-2]) << 12) |
                     (uint32_t(vals[vals.size()-1]) << 6);
        out.push_back(char((v >> 16) & 0xff));
        out.push_back(char((v >> 8) & 0xff));
    }
    return out;
}

} // namespace crypto
