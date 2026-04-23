#pragma once

#include <string>
#include <expected>

#include "domain/model/AtomicBlock.hpp"
#include "infrastructure/wallet/WalletHelper.hpp"
#include "domain/Types.hpp"

namespace utx::app::infrastructure::deploy {
    class DeployClient {
    public:
        explicit DeployClient(std::string base_url);

        [[nodiscard]]
        std::expected<domain::DeployResult, std::string>
        deploy(const domain::DeployRequest& req,
               const infra::wallet::KeyPair& wallet);
        
        [[nodiscard]]
        std::expected<nlohmann::json, std::string>
        prepare(const domain::DeployRequest& req, const std::string& sender);

        [[nodiscard]]
        std::expected<void, std::string>
        submit(const std::string& plan_id,
               const std::string& chain_id,
               const nlohmann::json& signed_txs);

        [[nodiscard]]
        static std::expected<utx::domain::model::SignedTransaction, std::string>
        build_signed_tx(const std::string& payload,
                        const infra::wallet::KeyPair& wallet);

    private:
        std::string base_url_;
    };
}
