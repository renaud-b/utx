#pragma once

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "Logger.hpp"

namespace utx::common {
    /** Join a vector of strings with a specified delimiter.
     * @param parts Vector of strings to join.
     * @param delimiter Delimiter to insert between strings.
     * @return Joined string.
     */
    [[nodiscard]]
    inline std::string join(const std::vector<std::string>& parts, const std::string& delimiter) {
        if (parts.empty()) return "";
        std::string result = parts[0];
        for (size_t i = 1; i < parts.size(); ++i) {
            result += delimiter + parts[i];
        }
        return result;
    }

    /** Convert a vector of bytes to a hexadecimal string.
     * @param data Vector of bytes to convert.
     * @return Hexadecimal string representation of the byte vector.
     */
     [[nodiscard]]
    inline std::string bytes_to_hex(const std::vector<unsigned char>& data) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (auto b : data) ss << std::setw(2) << static_cast<int>(b);
        return ss.str();
    }
    /** Convert a hexadecimal string to a vector of bytes.
     * @param hex Hexadecimal string to convert.
     * @return Vector of bytes represented by the hexadecimal string.
     */
    [[nodiscard]]
    inline std::vector<unsigned char> hex_to_bytes(const std::string& hex) {
        if (hex.size() % 2 != 0) {
            LOG_THIS_ERROR("Invalid hex string length: {}", hex.size());
            throw std::runtime_error("Invalid hex length");
        }
        std::vector<unsigned char> bytes;
        for (size_t i = 0; i < hex.length(); i += 2) {
            bytes.push_back(static_cast<unsigned char>(strtol(hex.substr(i, 2).c_str(), nullptr, 16)));
        }
        return bytes;
    }

}