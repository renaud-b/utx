#pragma once

#include <vector>
#include <cstdint>
#include <expected>
#include <string>
#include "../model/Paradox.hpp"

namespace utx::domain::port {

    class ISecretSharing {
    public:
        virtual ~ISecretSharing() = default;

        // Split secret into n shares, threshold t
        [[nodiscard]]
        virtual std::expected<std::vector<model::ParadoxFragment>, std::string>
        split(
            const std::string& key_id,
            const std::vector<uint8_t>& secret,
            uint32_t threshold,
            uint32_t total_shares,
            const model::Address& owner,
            const std::vector<model::Address>& acl
        ) = 0;

        // Reconstruct secret from shares
        [[nodiscard]]
        virtual std::expected<std::vector<uint8_t>, std::string>
        combine(const std::vector<model::ParadoxFragment>& shares) = 0;
    };

}
