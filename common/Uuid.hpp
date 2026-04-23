#pragma once
#include <string>
#include <array>
#include <chrono>
#include <random>

namespace utx::common {
    constexpr size_t UUID_BYTES = 16;
    constexpr size_t UUID_BASE64_LEN = 22;
    /** Represents a UUID, either in Base64URL (22 chars) or canonical (36 chars) format.
     * Inherits from std::string for easy manipulation and storage.
     */
    struct UUID : std::string {
        /** Get a UUID representing all zeros.
         * @returns A UUID with all bits set to zero.
         */
        static UUID zero() {
            return {"00000000-0000-0000-0000-000000000000"};
        }

        using std::string::string;

        /** Construct a UUID from a string.
         * @param basic_string The UUID string in Base64URL (22 chars) or canonical (36 chars) format.
         * @throws std::invalid_argument if the string is not a valid UUID format.
         */
        explicit UUID(const std::string & basic_string);
        /** Get the string representation of the UUID.
         * @returns The UUID as a string.
         */
        const std::string & to_string() const;
        /** Create a UUID from raw bytes.
         * @param bytes A 16-byte array representing the UUID.
         * @returns A UUID object in Base64URL format.
         */
        static UUID from_bytes(const std::array<uint8_t, UUID_BYTES>& bytes);
    };


    /** Generate a new UUIDv7.
     * @returns A newly generated UUIDv7 in Base64URL format.
     */
    UUID generate_uuid_v7();
    /** Decode a Base64URL-encoded UUID string into raw bytes.
     * @param encoded The Base64URL-encoded UUID string (22 chars).
     * @returns A 16-byte array representing the UUID.
     * @throws std::invalid_argument if the input string is not valid Base64URL or not 22 chars.
     */
    std::array<uint8_t, UUID_BYTES> decode_uuid_base64(const std::string& encoded);
    /** Extract the timestamp from a UUIDv7.
     * @param uuid The UUIDv7 in Base64URL or canonical format.
     * @returns The extracted timestamp as a std::chrono::system_clock::time_point.
     * @throws std::invalid_argument if the UUID is not valid or not a UUIDv7.
     */
    std::chrono::system_clock::time_point timestamp_from_uuid(const UUID& uuid);
    /** Encode raw bytes into a Base64URL-encoded UUID string.
     * @param bytes A 16-byte array representing the UUID.
     * @returns A UUID object in Base64URL format (22 chars).
     */
    UUID base64url_encode(const std::array<uint8_t, UUID_BYTES> &bytes);
    /** Validate if a string is a valid UUID format (Base64URL or canonical).
     * @param uuid The UUID string to validate.
     * @returns true if the string is a valid UUID format, false otherwise.
     */
    inline bool is_valid_uuid(const UUID& uuid) {
        return uuid.size() == 22 || uuid.size() == 36;
    }
    /** Generate a UUIDv7 with a specific timestamp.
     * @param tp The timestamp to embed in the UUIDv7.
     * @returns A UUIDv7 with the specified timestamp in Base64URL format.
     */
    inline UUID generate_uuid_with_time(const std::chrono::system_clock::time_point& tp) {
        using namespace std::chrono;

        const auto ms = duration_cast<milliseconds>(tp.time_since_epoch()).count();

        std::array<uint8_t, 16> bytes{};

        // Insert the timestamp in the first 6 bytes (48 bits)
        bytes[0] = static_cast<uint8_t>((ms >> 40) & 0xFF);
        bytes[1] = static_cast<uint8_t>((ms >> 32) & 0xFF);
        bytes[2] = static_cast<uint8_t>((ms >> 24) & 0xFF);
        bytes[3] = static_cast<uint8_t>((ms >> 16) & 0xFF);
        bytes[4] = static_cast<uint8_t>((ms >> 8) & 0xFF);
        bytes[5] = static_cast<uint8_t>((ms) & 0xFF);

        // Fill the remaining 10 bytes with random data
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<uint8_t> dist(0, 255);

        for (size_t i = 6; i < 16; ++i) {
            bytes[i] = dist(gen);
        }

        // Set version and variant bits
        bytes[6] = (bytes[6] & 0x0F) | 0x70; // version 7
        bytes[8] = (bytes[8] & 0x3F) | 0x80; // variant RFC 4122

        return UUID::from_bytes(bytes);
    }


} // namespace utx::common

template<>
struct std::hash<utx::common::UUID> {
    size_t operator()(const utx::common::UUID& uuid) const noexcept {
        return std::hash<std::string>()(uuid);
    }
};
