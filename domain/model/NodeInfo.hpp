#pragma once
#include "Types.hpp"
#include "../../infrastructure/wallet/WalletHelper.hpp"

namespace utx::domain::model {
    /**
     * @brief Represents information about a network node.
     * Immutable value object.
     */
    struct NodeInfo {
        Address id;       // Public address of the node
        std::string ip;   // IP address, e.g., "127.0.0.1"
        int port;         // 8080
        uint64_t reputation_score;

        mutable bool is_online = true;
        mutable uint64_t failures = 0;

        // For the self node, we can have the key pair
        std::optional<infra::wallet::KeyPair> key_pair = std::nullopt;

        auto operator<=>(const NodeInfo& other) const {
            return id <=> other.id;
        }

        static NodeInfo load_from_str(const std::string& payload) {
            // Expected format: "address@ip:port"
            auto at_pos = payload.find('@');
            auto colon_pos = payload.find(':', at_pos);
            if (at_pos == std::string::npos || colon_pos == std::string::npos) {
                throw std::invalid_argument("Invalid node info format. Expected 'address@ip:port'");
            }
            Address id(payload.substr(0, at_pos));
            std::string ip = payload.substr(at_pos + 1, colon_pos - at_pos - 1);
            int port = std::stoi(payload.substr(colon_pos + 1));
            return NodeInfo{id, ip, port, 0};
        }
    };

    inline void to_json(nlohmann::json& j, const NodeInfo& n) {
        j = nlohmann::json{
            {"id", n.id},
            {"ip", n.ip},
            {"port", n.port},
            {"reputation_score", n.reputation_score},
            {"is_online", n.is_online},
            {"failures", n.failures}
        };
    }

    inline void from_json(const nlohmann::json& j, NodeInfo& n) {
        n.id = j.at("id").get<Address>();
        n.ip = j.at("ip").get<std::string>();
        n.port = j.at("port").get<int>();
        n.reputation_score = j.value("reputation_score", 0UL);
        n.is_online = j.value("is_online", true);
        n.failures = j.value("failures", 0UL);
    }
}
