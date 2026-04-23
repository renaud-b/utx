#pragma once
#include <format>
#include <string>
#include <vector>
#include <stdexcept>
#include <format>
#include <nlohmann/json.hpp>

namespace utx::domain::model {

    /**
     * @brief Represents a network address (Identity).
     * Immutable value object ensuring format validity.
     */
    class Address {
    public:
        Address() = default;

        explicit Address(std::string value) : value_(std::move(value)) {
            // Production Rule: Validate format (e.g., must start with '0x', length check)
            if (value_.empty()) {
                throw std::invalid_argument("Address cannot be empty");
            }
        }

        [[nodiscard]] std::string to_string() const { return value_; }
        
        // Operators for map keys comparison
        auto operator<=>(const Address&) const = default;

    private:
        std::string value_;
    };

    inline void to_json(nlohmann::json& j, const Address& a) {
        j = a.to_string();
    }

    inline void from_json(const nlohmann::json& j, Address& a) {
        a = Address(j.get<std::string>());
    }

    /**
     * @brief Alias for ChainAddress to represent blockchain addresses.
     */
    typedef Address ChainAddress;

    /**
     * @brief Represents a cryptographic hash (SHA-256 / SHA-3).
     */
    class Hash {
    public:
        Hash() = default;
        explicit Hash(std::string hex_value) : hex_value_(std::move(hex_value)) {}

        [[nodiscard]] std::string to_string() const { return hex_value_; }
        
        // Special value for Genesis parent
        static Hash genesis() { return Hash("0000000000000000000000000000000000000000000000000000000000000000"); }
        
        auto operator<=>(const Hash&) const = default;

        // Defined for unordered_map key comparison
        bool operator==(const Hash& other) const {
            return hex_value_ == other.hex_value_;
        }

    private:
        std::string hex_value_;
    };
    // Hash <-> String
    inline void to_json(nlohmann::json& j, const Hash& h) {
        j = h.to_string();
    }
    inline void from_json(const nlohmann::json& j, Hash& h) {
        h = Hash(j.get<std::string>());
    }

    /**
     * @brief Represents a digital signature (Container for bytes).
     */
    class Signature {
    public:
        Signature() = default;
        explicit Signature(std::string hex_data) : data_(std::move(hex_data)) {}
        [[nodiscard]] std::string to_string() const { return data_; }
        auto operator<=>(const Signature&) const = default;
    private:
        std::string data_;
    };

    // Signature <-> String
    inline void to_json(nlohmann::json& j, const Signature& s) {
        j = s.to_string();
    }
    inline void from_json(const nlohmann::json& j, Signature& s) {
        s = Signature(j.get<std::string>());
    }

    struct ParadoxCommand {
        enum Type {
            CREATE_KEY,
            DELETE_KEY,
            UPDATE_ACL
        };

        Type type;
        std::string key_id;
        uint32_t threshold;
        uint32_t total_shares;
        std::vector<Address> acl;

        [[nodiscard]]
        std::string to_string() const {
            return std::format("ParadoxCommand{{type={}, key_id={}, threshold={}, total_shares={}, acl=[{}]}}",
                               type == CREATE_KEY ? "CREATE_KEY" : (type == DELETE_KEY ? "DELETE_KEY" : "UPDATE_ACL"),
                               key_id, threshold, total_shares,
                               std::accumulate(acl.begin(), acl.end(), std::string(),
                                               [](const std::string& acc, const Address& addr) {
                                                   return acc.empty() ? addr.to_string() : acc + ", " + addr.to_string();
                                               }));
        }

        [[nodiscard]]
        nlohmann::json to_json() const {
            std::vector<std::string> acl_strings;
            for (const auto& addr : acl)
                acl_strings.push_back(addr.to_string());

            return nlohmann::json{
                {"type", type == CREATE_KEY ? "CREATE_KEY" : (type == DELETE_KEY ? "DELETE_KEY" : "UPDATE_ACL")},
                {"key_id", key_id},
                {"threshold", threshold},
                {"total_shares", total_shares},
                {"acl", acl_strings}
            };
        }
    };

}

// Formatter specialization for std::format (C++20/23)
template <>
struct std::formatter<utx::domain::model::Address> : std::formatter<std::string> {
    auto format(const utx::domain::model::Address& a, format_context& ctx) const {
        return formatter<std::string>::format(a.to_string(), ctx);
    }
};

template <>
struct std::formatter<utx::domain::model::Hash> : std::formatter<std::string> {
    auto format(const utx::domain::model::Hash& h, format_context& ctx) const {
        return formatter<std::string>::format(h.to_string(), ctx);
    }
};

namespace std {
    /** Specialization of std::hash for utx::domain::model::Hash
     * to allow usage in unordered containers.
     */
    template <>
    struct hash<utx::domain::model::Hash> {
        size_t operator()(const utx::domain::model::Hash& h) const noexcept {
            return std::hash<std::string>{}(h.to_string());
        }
    };

    template <>
    struct hash<utx::domain::model::Address> {
        size_t operator()(const utx::domain::model::Address& addr) const noexcept {
            return std::hash<std::string>()(addr.to_string());
        }
    };
}