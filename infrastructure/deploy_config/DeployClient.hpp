#pragma once

#include <chrono>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "domain/model/AtomicBlock.hpp"
#include "infrastructure/wallet/WalletHelper.hpp"
#include "domain/Types.hpp"

namespace utx::app::infrastructure::deploy {

    struct DeploySubmission {
        std::vector<std::string> pending_block_ids;
        std::optional<std::string> admission_error;
    };

    struct PendingBlockStatus {
        std::string id;
        std::string chain_address;
        std::string state;
        std::optional<std::string> error;
    };

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

        /**
         * Current deploy protocol: a successful submit means admission only.
         * The returned pending block ids must be observed until Finalized.
         */
        [[nodiscard]]
        std::expected<DeploySubmission, std::string>
        submit(const std::string& plan_id,
               const std::string& chain_id,
               const nlohmann::json& ring_reference,
               const nlohmann::json& signed_txs);

        [[nodiscard]]
        std::expected<PendingBlockStatus, std::string>
        get_pending_status(const std::string& chain_id,
                           const std::string& pending_block_id) const;

        [[nodiscard]]
        std::expected<void, std::string>
        wait_for_finalization(
            const std::string& chain_id,
            const std::vector<std::string>& pending_block_ids,
            std::chrono::milliseconds timeout = std::chrono::minutes(5),
            std::chrono::milliseconds poll_interval = std::chrono::milliseconds(250)
        ) const;

        [[nodiscard]]
        static std::expected<utx::domain::model::SignedTransaction, std::string>
        build_signed_tx(const std::string& payload,
                        const std::string& chain_id,
                        const infra::wallet::KeyPair& wallet);

    private:
        std::string base_url_;
    };
}
