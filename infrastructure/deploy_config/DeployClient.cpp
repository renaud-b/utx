#include <chrono>
#include <format>
#include <random>
#include <thread>

#include <httplib.h>

#include "DeployClient.hpp"
#include "common/Hash.hpp"
#include "common/ScopedTimer.hpp"
#include "common/Uuid.hpp"
#include "infrastructure/crypto/OpenSSLCryptoService.hpp"

namespace utx::app::infrastructure::deploy {

    namespace {
        std::expected<utx::domain::model::SignedTransaction, std::string>
        build_signed_tx_for_receiver(
            const std::string& payload,
            const std::string& receiver,
            const infra::wallet::KeyPair& wallet
        ) {
            using namespace utx::domain::model;

            try {
                SignedTransaction tx;
                tx.sender = Address(wallet.address);
                tx.receiver = Address(receiver);
                tx.amount = 0;
                tx.nonce = std::random_device{}();
                tx.data = payload;
                tx.sender_public_key = wallet.public_key_hex;

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
                return std::unexpected(
                    std::string("Failed to build signed tx: ") + e.what()
                );
            }
        }
    }

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
            }
        );

        if (!res) {
            return std::unexpected(
                std::format("HTTP error (prepare): {}", httplib::to_string(res.error()))
            );
        }
        if (res->status != 200) {
            return std::unexpected(
                std::format("Prepare failed: HTTP {} - {}", res->status, res->body)
            );
        }

        try {
            return nlohmann::json::parse(res->body);
        } catch (const std::exception& e) {
            return std::unexpected(std::string("Invalid JSON: ") + e.what());
        }
    }

    std::expected<utx::domain::model::SignedTransaction, std::string>
    DeployClient::build_signed_tx(
        const std::string& payload,
        const std::string& chain_id,
        const infra::wallet::KeyPair& wallet
    ) {
        return build_signed_tx_for_receiver(payload, chain_id, wallet);
    }

    std::expected<utx::domain::model::SignedTransaction, std::string>
    DeployClient::build_signed_tx(
        const std::string& payload,
        const infra::wallet::KeyPair& wallet
    ) {
        return build_signed_tx_for_receiver(payload, wallet.address, wallet);
    }

    std::expected<DeploySubmission, std::string>
    DeployClient::submit(
        const std::string& plan_id,
        const std::string& chain_id,
        const nlohmann::json& ring_reference,
        const nlohmann::json& signed_txs
    ) {
        httplib::Client cli(base_url_);
        cli.set_read_timeout(60, 0);
        cli.set_connection_timeout(10, 0);

        nlohmann::json body = {
            {"plan_id", plan_id},
            {"ring_reference", ring_reference},
            {"chain_id", chain_id},
            {"signed_transactions", signed_txs}
        };

        return common::ScopedTimer::measure(
            std::format("DeployClient::submit - HTTP POST {}/api/deploy/submit", base_url_),
            std::chrono::milliseconds(50),
            [&]() -> std::expected<DeploySubmission, std::string> {
                auto res = cli.Post(
                    "/api/deploy/submit",
                    body.dump(),
                    "application/json"
                );

                if (!res) {
                    return std::unexpected(
                        std::format(
                            "HTTP error (submit): {}",
                            httplib::to_string(res.error())
                        )
                    );
                }

                if (res->status != 202) {
                    return std::unexpected(
                        std::format(
                            "Submit failed: HTTP {} - {}",
                            res->status,
                            res->body
                        )
                    );
                }

                try {
                    const auto response = nlohmann::json::parse(res->body);
                    const auto status = response.value("status", "");

                    if (status != "queued" && status != "partially_queued") {
                        return std::unexpected(
                            "Invalid submit response: unexpected status '" +
                            status + "'"
                        );
                    }

                    if (!response.contains("pending_block_ids") ||
                        !response.at("pending_block_ids").is_array()) {
                        return std::unexpected(
                            "Invalid submit response: missing pending_block_ids"
                        );
                    }

                    DeploySubmission submission;
                    submission.pending_block_ids =
                        response.at("pending_block_ids")
                            .get<std::vector<std::string>>();

                    if (response.contains("admission_error") &&
                        response.at("admission_error").is_string()) {
                        submission.admission_error =
                            response.at("admission_error").get<std::string>();
                    }

                    return submission;
                } catch (const std::exception& e) {
                    return std::unexpected(
                        std::string("Invalid submit response: ") + e.what()
                    );
                }
            }
        );
    }

    std::expected<void, std::string>
    DeployClient::submit(
        const std::string& plan_id,
        const std::string& chain_id,
        const nlohmann::json& signed_txs
    ) {
        httplib::Client cli(base_url_);
        cli.set_read_timeout(60, 0);
        cli.set_connection_timeout(10, 0);

        nlohmann::json body = {
            {"plan_id", plan_id},
            {"chain_id", chain_id},
            {"signed_transactions", signed_txs}
        };

        auto res = cli.Post(
            "/api/deploy/submit",
            body.dump(),
            "application/json"
        );

        if (!res) {
            return std::unexpected(
                std::format(
                    "HTTP error (legacy submit): {}",
                    httplib::to_string(res.error())
                )
            );
        }

        if (res->status != 200) {
            return std::unexpected(
                std::format(
                    "Legacy submit failed: HTTP {} - {}",
                    res->status,
                    res->body
                )
            );
        }

        return {};
    }

    std::expected<PendingBlockStatus, std::string>
    DeployClient::get_pending_status(
        const std::string& chain_id,
        const std::string& pending_block_id
    ) const {
        httplib::Client cli(base_url_);
        cli.set_read_timeout(20, 0);
        cli.set_connection_timeout(10, 0);

        auto res = cli.Get(
            std::format(
                "/chain/{}/pending/{}",
                chain_id,
                pending_block_id
            )
        );

        if (!res) {
            return std::unexpected(
                std::format(
                    "HTTP error (pending status): {}",
                    httplib::to_string(res.error())
                )
            );
        }

        if (res->status != 200) {
            return std::unexpected(
                std::format(
                    "Pending status failed for {}: HTTP {} - {}",
                    pending_block_id,
                    res->status,
                    res->body
                )
            );
        }

        try {
            const auto body = nlohmann::json::parse(res->body);

            PendingBlockStatus status{
                .id = body.at("id").get<std::string>(),
                .chain_address = body.at("chain_address").get<std::string>(),
                .state = body.at("state").get<std::string>(),
                .error = std::nullopt
            };

            if (body.contains("error") && body.at("error").is_object()) {
                const auto& error = body.at("error");
                if (error.contains("message") && error.at("message").is_string()) {
                    status.error = error.at("message").get<std::string>();
                }
            }

            return status;
        } catch (const std::exception& e) {
            return std::unexpected(
                std::string("Invalid pending status response: ") + e.what()
            );
        }
    }

    std::expected<void, std::string>
    DeployClient::wait_for_finalization(
        const std::string& chain_id,
        const std::vector<std::string>& pending_block_ids,
        const std::chrono::milliseconds timeout,
        const std::chrono::milliseconds poll_interval
    ) const {
        if (pending_block_ids.empty()) {
            return std::unexpected(
                "Cannot wait for finalization without pending block ids"
            );
        }

        const auto deadline = std::chrono::steady_clock::now() + timeout;

        while (std::chrono::steady_clock::now() < deadline) {
            bool all_finalized = true;

            for (const auto& pending_id : pending_block_ids) {
                auto status = get_pending_status(chain_id, pending_id);
                if (!status) {
                    return std::unexpected(status.error());
                }

                if (status->state == "Finalized") {
                    continue;
                }

                all_finalized = false;

                if (status->state == "Rejected" ||
                    status->state == "Expired") {
                    const auto detail =
                        status->error.value_or("no additional error");

                    return std::unexpected(
                        std::format(
                            "Pending block {} ended in {}: {}",
                            pending_id,
                            status->state,
                            detail
                        )
                    );
                }

                // CommitUncertain is intentionally not considered a definitive
                // failure by the node. Keep observing it until the client-side
                // timeout, just like the other non-terminal states.
            }

            if (all_finalized) {
                return {};
            }

            std::this_thread::sleep_for(poll_interval);
        }

        return std::unexpected(
            std::format(
                "Timed out waiting for {} pending block(s) on chain {}",
                pending_block_ids.size(),
                chain_id
            )
        );
    }

    std::expected<domain::DeployResult, std::string>
    DeployClient::deploy(
        const domain::DeployRequest& req,
        const infra::wallet::KeyPair& wallet
    ) {
        auto plan_res = prepare(req, wallet.address);
        if (!plan_res) {
            return std::unexpected(plan_res.error());
        }

        const auto& plan = plan_res.value();

        if (!plan.contains("transactions") ||
            !plan.at("transactions").is_array()) {
            return std::unexpected(
                "Invalid plan: missing transactions"
            );
        }

        if (!plan.contains("ring_reference") ||
            !plan.at("ring_reference").is_object()) {
            return std::unexpected(
                "Invalid plan: missing ring_reference"
            );
        }

        nlohmann::json signed_txs = nlohmann::json::array();

        for (const auto& tx : plan.at("transactions")) {
            const auto payload = tx.at("payload_data").get<std::string>();

            auto signed_tx_res =
                build_signed_tx(payload, req.chain_id, wallet);

            if (!signed_tx_res) {
                return std::unexpected(signed_tx_res.error());
            }

            signed_txs.push_back(std::move(signed_tx_res.value()));
        }

        auto submit_res = submit(
            plan.at("plan_id").get<std::string>(),
            req.chain_id,
            plan.at("ring_reference"),
            signed_txs
        );

        if (!submit_res) {
            return std::unexpected(submit_res.error());
        }

        if (submit_res->admission_error) {
            return std::unexpected(
                "Deploy was only partially queued: " +
                *submit_res->admission_error
            );
        }

        if (submit_res->pending_block_ids.size() != signed_txs.size()) {
            return std::unexpected(
                std::format(
                    "Deploy queued {}/{} transactions",
                    submit_res->pending_block_ids.size(),
                    signed_txs.size()
                )
            );
        }

        auto finalized = wait_for_finalization(
            req.chain_id,
            submit_res->pending_block_ids
        );

        if (!finalized) {
            return std::unexpected(finalized.error());
        }

        return domain::DeployResult{
            .success = true,
            .plan_id = plan.at("plan_id").get<std::string>()
        };
    }
}
