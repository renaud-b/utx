#include <httplib.h>

#include "DeployClient.hpp"
#include "common/Hash.hpp"
#include "common/ScopedTimer.hpp"
#include "common/Uuid.hpp"
#include "infrastructure/crypto/OpenSSLCryptoService.hpp"

namespace utx::app::infrastructure::deploy {
    DeployClient::DeployClient(std::string base_url)
    : base_url_(std::move(base_url)) {}

    std::expected<nlohmann::json, std::string>
    DeployClient::prepare(const domain::DeployRequest& req, const std::string& sender)
    {
        httplib::Client cli(base_url_);
        cli.set_read_timeout(20, 0);
        cli.set_connection_timeout(10, 0);
        nlohmann::json body = {
            {"chain_id", req.chain_id},
            {"file_path", req.file_path},
            {"sender_address", sender},
            {"content", req.content},
            {"commit_message", req.commit_message},
            {"force_snapshot", req.force_snapshot}
        };
        if (!req.projector.empty()) {
            body["projector"] = req.projector;
        }
        if (!req.kind.empty()) {
            body["kind"] = req.kind;
        }
        auto res = common::ScopedTimer::measure(
            std::format("DeployClient::prepare - HTTP POST {}/api/deploy/prepare", base_url_),
            std::chrono::seconds(1),
            [&]() {
                return cli.Post("/api/deploy/prepare", body.dump(), "application/json");
            });

        if (!res) return std::unexpected(std::format("HTTP error (prepare): {}", httplib::to_string(res.error())));
        if (res->status != 200) return std::unexpected(std::format("Prepare failed: HTTP {} - {}", res->status, res->body));

        try {
            return nlohmann::json::parse(res->body);
        } catch (const std::exception& e) {
            return std::unexpected(std::string("Invalid JSON: ") + e.what());
        }
    }

    std::expected<utx::domain::model::SignedTransaction, std::string>
    DeployClient::build_signed_tx(const std::string& payload,
                const infra::wallet::KeyPair& wallet)
    {
        using namespace utx::domain::model;

        try {
            SignedTransaction tx;
            tx.sender = Address(wallet.address);
            tx.receiver = Address(wallet.address);
            tx.amount = 0;
            tx.nonce = std::random_device{}();
            tx.data = payload;
            tx.sender_public_key = wallet.public_key_hex;

            // 🔥 EXACT même logique que ton NetworkClient
            const auto serialized = tx.serialize_for_signing();
            const auto tx_hash = common::sha256_hex(serialized);

            tx.signature = Signature(
                infra::wallet::WalletHelper::sign_message(
                    wallet.private_key_hex,
                    tx_hash
                )
            );

            return tx;

        } catch (const std::exception& e) {
            return std::unexpected(std::string("Failed to build signed tx: ") + e.what());
        }
    }

    std::expected<void, std::string>
    DeployClient::submit(const std::string& plan_id,
                     const std::string& chain_id,
                     const nlohmann::json& signed_txs)
    {
        httplib::Client cli(base_url_);
        cli.set_read_timeout(60, 0);
        cli.set_connection_timeout(10, 0);

        nlohmann::json body = {
            {"plan_id", plan_id},
            {"chain_id", chain_id},
            {"signed_transactions", signed_txs}
        };

        return common::ScopedTimer::measure(
            std::format("DeployClient::submit - HTTP POST {}/api/deploy/submit", base_url_),
            std::chrono::milliseconds(50),
            [&] -> std::expected<void, std::string> {
                auto res = cli.Post("/api/deploy/submit", body.dump(), "application/json");

                if (!res) return std::unexpected("HTTP error (submit)");
                if (res->status != 200) return std::unexpected("Submit failed: " + res->body);

                return {};
            }
        );

    }

    std::expected<domain::DeployResult, std::string>
    DeployClient::deploy(const domain::DeployRequest& req,
                     const infra::wallet::KeyPair& wallet)
    {

        // 1️⃣ PREPARE
        auto plan_res = prepare(req, wallet.address);
        if (!plan_res) return std::unexpected(plan_res.error());

        const auto& plan = plan_res.value();

        if (!plan.contains("transactions") || !plan["transactions"].is_array()) {
            return std::unexpected("Invalid plan: missing transactions");
        }

        std::vector<utx::domain::model::SignedTransaction> signed_txs;
        for (const auto& tx : plan["transactions"]) {
            const auto payload = tx["payload_data"].get<std::string>();

            auto signed_tx_res = build_signed_tx(payload, wallet);
            if (!signed_tx_res) {
                return std::unexpected(signed_tx_res.error());
            }

            signed_txs.push_back(signed_tx_res.value());
        }

        // 3️⃣ SUBMIT
        auto submit_res = submit(plan["plan_id"], req.chain_id, signed_txs);
        if (!submit_res) return std::unexpected(submit_res.error());

        return domain::DeployResult{
            .success = true,
            .plan_id = plan["plan_id"]
        };
    }
}
