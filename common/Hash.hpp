// common/Hash.hpp
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace utx::common {
    /** Compute the MD5 hash of the input string and return it as a hexadecimal string.
     * @param input The input string to hash.
     * @returns The MD5 hash as a hexadecimal string.
     */
    std::string md5_hex(const std::string& input);
    /** Compute the SHA-256 hash of the input data and return it as a hexadecimal string.
     * @param data Pointer to the input data bytes.
     * @param len Length of the input data in bytes.
     * @returns The SHA-256 hash as a hexadecimal string.
     */
    std::string sha256_hex(const uint8_t* data, size_t len);
    /** Compute the SHA-256 hash of the input byte vector and return it as a hexadecimal string.
     * @param input The input byte vector to hash.
     * @returns The SHA-256 hash as a hexadecimal string.
     */
    std::string sha256_hex(const std::vector<uint8_t>& input);
    /** Compute the SHA-256 hash of the input string and return it as a hexadecimal string.
     * @param input The input string to hash.
     * @returns The SHA-256 hash as a hexadecimal string.
     */
    std::string sha256_hex(const std::string& input);
    /** Compute the SHA-256 hash of the contents of a file specified by its path and return it as a hexadecimal string.
     * @param path The filesystem path to the file to hash.
     * @returns The SHA-256 hash of the file contents as a hexadecimal string.
     */
    std::string sha256_file_hex(const std::filesystem::path& path);


}
