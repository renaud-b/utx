#include "Hash.hpp"
#include <array>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <openssl/sha.h>

namespace {
    constexpr uint32_t INIT_A = 0x67452301u;
    constexpr uint32_t INIT_B = 0xefcdab89u;
    constexpr uint32_t INIT_C = 0x98badcfeu;
    constexpr uint32_t INIT_D = 0x10325476u;

    uint32_t F(const uint32_t x, const uint32_t y, const uint32_t z) { return (x & y) | (~x & z); }
    uint32_t G(const uint32_t x, const uint32_t y, const uint32_t z) { return (x & z) | (y & ~z); }
    uint32_t H(const uint32_t x, const uint32_t y, const uint32_t z) { return x ^ y ^ z; }
    uint32_t I(const uint32_t x, const uint32_t y, const uint32_t z) { return y ^ (x | ~z); }
    uint32_t left_rotate(const uint32_t x, const uint32_t c) { return (x << c) | (x >> (32 - c)); }

    constexpr uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
    };

    const uint32_t S[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5,9,14,20, 5,9,14,20, 5,9,14,20, 5,9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
    };
}

namespace utx::common {
    std::string md5_hex(const std::string& input) {
        const uint64_t bit_len = input.size() * 8;
        std::vector<uint8_t> msg(input.begin(), input.end());
        msg.push_back(0x80);
        while ((msg.size() % 64) != 56) msg.push_back(0);
        for (int i = 0; i < 8; ++i) msg.push_back(static_cast<uint8_t>(bit_len >> (8 * i)));

        uint32_t a = INIT_A, b = INIT_B, c = INIT_C, d = INIT_D;

        for (size_t offset = 0; offset < msg.size(); offset += 64) {
            uint32_t M[16];
            for (int i = 0; i < 16; ++i) {
                M[i] = static_cast<uint32_t>(msg[offset + i*4]) |
                       (static_cast<uint32_t>(msg[offset + i*4+1]) << 8) |
                       (static_cast<uint32_t>(msg[offset + i*4+2]) << 16) |
                       (static_cast<uint32_t>(msg[offset + i*4+3]) << 24);
            }

            uint32_t A = a, B = b, C = c, D = d;

            for (int i = 0; i < 64; ++i) {
                uint32_t F_val, g;
                if (i < 16) { F_val = F(B,C,D); g = i; }
                else if (i < 32) { F_val = G(B,C,D); g = (5*i+1)%16; }
                else if (i < 48) { F_val = H(B,C,D); g = (3*i+5)%16; }
                else { F_val = I(B,C,D); g = (7*i)%16; }

                uint32_t tmp = D;
                D = C;
                C = B;
                B = B + left_rotate(A + F_val + K[i] + M[g], S[i]);
                A = tmp;
            }

            a += A; b += B; c += C; d += D;
        }

        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (uint32_t x : {a,b,c,d}) {
            for (int i = 0; i < 4; ++i)
                oss << std::setw(2) << ((x >> (8 * i)) & 0xff);
        }
        return oss.str();
    }


    std::string sha256_hex(const uint8_t* data, size_t len)
    {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(data, len, hash);

        static constexpr char hexmap[] = "0123456789abcdef";
        std::string out;
        out.resize(SHA256_DIGEST_LENGTH * 2);

        for (size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            out[2*i]     = hexmap[(hash[i] >> 4) & 0xF];
            out[2*i + 1] = hexmap[hash[i] & 0xF];
        }

        return out;
    }

    std::string sha256_hex(const std::string& input)
    {
        return sha256_hex(
            reinterpret_cast<const uint8_t*>(input.data()),
            input.size()
        );
    }

    std::string sha256_hex(const std::vector<uint8_t>& input)
    {
        return sha256_hex(input.data(), input.size());
    }



    std::string sha256_file_hex(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);

        if (!file)
            throw std::runtime_error("cannot open file for hashing");

        SHA256_CTX ctx;
        SHA256_Init(&ctx);

        std::vector<uint8_t> buffer(64 * 1024);

        while (file)
        {
            file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());

            if (file.gcount() > 0)
                SHA256_Update(&ctx, buffer.data(), file.gcount());
        }

        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_Final(hash, &ctx);

        static constexpr char hexmap[] = "0123456789abcdef";

        std::string out;
        out.resize(SHA256_DIGEST_LENGTH * 2);

        for (size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        {
            out[2*i]     = hexmap[(hash[i] >> 4) & 0xF];
            out[2*i + 1] = hexmap[hash[i] & 0xF];
        }

        return out;
    }

}
