#include "Uuid.hpp"
#include <sodium.h>
#include <stdexcept>
#include <array>
#include <chrono>
#include <string>

#include "Logger.hpp"

namespace utx::common {
    UUID base64url_encode(const std::array<uint8_t, UUID_BYTES> &bytes) {
        unsigned char tmp[24]; // Base64 output
        sodium_bin2base64(reinterpret_cast<char*>(tmp), sizeof(tmp),
                          bytes.data(), UUID_BYTES,
                          sodium_base64_VARIANT_URLSAFE_NO_PADDING);
        return UUID(reinterpret_cast<char*>(tmp));
    }

    std::array<uint8_t, UUID_BYTES> decode_uuid_base64(const std::string& encoded) {
        if (encoded.size() != UUID_BASE64_LEN) {
            LOG_THIS_ERROR("Invalid UUID length: expected {}, got {}, for '{}'",
                           UUID_BASE64_LEN, encoded.size(), encoded);
            throw std::invalid_argument("Invalid UUID length");
        }
        std::array<uint8_t, UUID_BYTES> bytes{};
        size_t out_len = 0;
        if (sodium_base642bin(bytes.data(), bytes.size(),
                               encoded.c_str(), encoded.size(),
                               nullptr, &out_len, nullptr,
                               sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0) {
            throw std::invalid_argument("Invalid Base64URL encoding");
        }
        return bytes;
    }

    UUID::UUID(const std::string &basic_string) {
        this->assign(basic_string);
    }

    const std::string & UUID::to_string() const {
        return *this;
    }

    UUID UUID::from_bytes(const std::array<uint8_t, UUID_BYTES> &bytes) {
        return base64url_encode(bytes);
    }

    UUID generate_uuid_v7() {
        std::array<uint8_t, UUID_BYTES> uuid{};
        randombytes_buf(uuid.data(), uuid.size());

        // Insert timestamp in first 6 bytes (48 bits)
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch()).count();
        uuid[0] = (now_ms >> 40) & 0xFF;
        uuid[1] = (now_ms >> 32) & 0xFF;
        uuid[2] = (now_ms >> 24) & 0xFF;
        uuid[3] = (now_ms >> 16) & 0xFF;
        uuid[4] = (now_ms >> 8) & 0xFF;
        uuid[5] = (now_ms) & 0xFF;

        // UUIDv7 variant + version
        uuid[6] = (uuid[6] & 0x0F) | 0x70; // version 7
        uuid[8] = (uuid[8] & 0x3F) | 0x80; // variant

        return UUID(base64url_encode(uuid));
    }

    std::chrono::system_clock::time_point timestamp_from_uuid(const UUID& uuid) {
        const auto bytes = decode_uuid_base64(uuid);
        const uint64_t ts_ms =
            (static_cast<uint64_t>(bytes[0]) << 40) |
            (static_cast<uint64_t>(bytes[1]) << 32) |
            (static_cast<uint64_t>(bytes[2]) << 24) |
            (static_cast<uint64_t>(bytes[3]) << 16) |
            (static_cast<uint64_t>(bytes[4]) << 8)  |
            (static_cast<uint64_t>(bytes[5]));
        return std::chrono::system_clock::time_point{std::chrono::milliseconds(ts_ms)};
    }
} // namespace utx::common
