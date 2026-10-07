#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <random>
#include <thread>

#include <httplib.h>

#include "DeployClient.hpp"
#include "common/Hash.hpp"
#include "common/Logger.hpp"
#include "common/ScopedTimer.hpp"
#include "common/Uuid.hpp"
#include "infrastructure/crypto/OpenSSLCryptoService.hpp"

namespace utx::app::infrastructure::deploy {

    namespace {
        constexpr std::array kSubmitRetryDelays{
            std::chrono::milliseconds(100),
            std::chrono::milliseconds(300),
            std::chrono::milliseconds(700)
        };

        std::mutex pending_submission_mutex;

        [[nodiscard]]
        bool is_retryable_submit_status(const int status) {
            return status == 408 ||
                   status == 429 ||
                   status == 502 ||
                   status == 503 ||
                   status == 504;
        }

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

    DeployClient::DeployClient(
        std::string base_url,
        std::optional<std::filesystem::path> state_root
    )
        : base_url_(std::move(base_url)),
          state_root_(std::move(state_root)) {}

    std::optional<std::filesystem::path>
    DeployClient::pending_submission_path(
        const std::string& plan_id
    ) const {
        if (!state_root_) {
            return std::nullopt;
        }

        return *state_root_ /
            ".utx" /
            "pending-deploy-submissions" /
            (common::sha256_hex(plan_id) + ".json");
    }

    std::expected<void, std::string>
    DeployClient::persist_pending_submission(
        const std::string& plan_id,
        const std::string& chain_id,
        const nlohmann::json& ring_reference,
        const nlohmann::json& signed_txs
    ) const {
        const auto path = pending_submission_path(plan_id);
        if (!path) {
            return {};
        }

        try {
            std::filesystem::create_directories(path->parent_path());

            const auto tmp =
                path->string() + ".tmp-" +
                common::generate_uuid_v7().to_string();

            {
                std::ofstream out(tmp, std::ios::trunc);
                if (!out) {
                    return std::unexpected(
                        "Failed to open pending deploy submission temp file: " +
                        tmp
                    );
                }

                out << nlohmann::json{
                    {"version", 1},
                    {"plan_id", plan_id},
                    {"chain_id", chain_id},
                    {"ring_reference", ring_reference},
                    {"signed_transactions", signed_txs}
                }.dump(2);
                out.flush();

                if (!out) {
                    return std::unexpected(
                        "Failed to write pending deploy submission temp file: " +
                        tmp
                    );
                }
            }

            std::error_code ec;
            std::filesystem::rename(tmp, *path, ec);
            if (ec) {
                std::error_code ignored;
                std::filesystem::remove(*path, ignored);
                std::filesystem::rename(tmp, *path, ec);
            }

            if (ec) {
                std::error_code ignored;
                std::filesystem::remove(tmp, ignored);
                return std::unexpected(
                    "Failed to persist pending deploy submission: " +
                    ec.message()
                );
            }

            return {};
        } catch (const std::exception& e) {
            return std::unexpected(
                std::string(
                    "Failed to persist pending deploy submission: "
                ) + e.what()
            );
        }
    }

    std::expected<void, std::string>
    DeployClient::clear_pending_submission(
        const std::string& plan_id
    ) const {
        const auto path = pending_submission_path(plan_id);
        if (!path) {
            return {};
        }

        try {
            std::lock_guard lock(pending_submission_mutex);
            std::error_code ec;
            std::filesystem::remove(*path, ec);
            if (ec) {
                return std::unexpected(
                    "Failed to clear pending deploy submission: " +
                    ec.message()
                );
            }
            return {};
        } catch (const std::exception& e) {
            return std::unexpected(
                std::string(
                    "Failed to clear pending deploy submission: "
                ) + e.what()
            );
        }
    }

    std::expected<nlohmann::json, std::string>
    DeployClient::load_or_create_signed_transactions(
        const std::string& plan_id,
        const std::string& chain_id,
        const nlohmann::json& ring_reference,
        const nlohmann::json& transactions,
        const infra::wallet::KeyPair& wallet
    ) const {
        std::lock_guard lock(pending_submission_mutex);

        const auto path = pending_submission_path(plan_id);
        if (path && std::filesystem::exists(*path)) {
            try {
                std::ifstream in(*path);
                if (!in) {
                    return std::unexpected(
                        "Failed to open pending deploy submission: " +
                        path->string()
                    );
                }

                const auto cached = nlohmann::json::parse(in);

                if (cached.value("version", 0) != 1 ||
                    cached.value("plan_id", "") != plan_id ||
                    cached.value("chain_id", "") != chain_id ||
                    !cached.contains("ring_reference") ||
                    cached.at("ring_reference") != ring_reference ||
                    !cached.contains("signed_transactions") ||
                    !cached.at("signed_transactions").is_array()) {
                    return std::unexpected(
                        "Pending deploy submission cache does not match prepared plan"
                    );
                }

                const auto& signed_txs =
                    cached.at("signed_transactions");

                if (signed_txs.size() != transactions.size()) {
                    return std::unexpected(
                        "Pending deploy submission cache transaction count mismatch"
                    );
                }

                for (std::size_t i = 0; i < transactions.size(); ++i) {
                    const auto payload =
                        transactions.at(i)
                            .at("payload_data")
                            .get<std::string>();

                    const auto signed_tx =
                        signed_txs.at(i)
                            .get<utx::domain::model::SignedTransaction>();

                    if (signed_tx.sender.to_string() != wallet.address ||
                        signed_tx.receiver.to_string() != chain_id ||
                        signed_tx.sender_public_key != wallet.public_key_hex ||
                        signed_tx.data != payload) {
                        return std::unexpected(
                            "Pending deploy submission cache transaction mismatch"
                        );
                    }
                }

                return signed_txs;
            } catch (const std::exception& e) {
                return std::unexpected(
                    std::string(
                        "Failed to load pending deploy submission: "
                    ) + e.what()
                );
            }
        }

        nlohmann::json signed_txs = nlohmann::json::array();
        for (const auto& tx : transactions) {
            const auto payload =
                tx.at("payload_data").get<std::string>();

            auto signed_tx_res =
                build_signed_tx(payload, chain_id, wallet);

            if (!signed_tx_res) {
                return std::unexpected(signed_tx_res.error());
            }

            signed_txs.push_back(
                std::move(signed_tx_res.value())
            );
        }

        const auto persisted = persist_pending_submission(
            plan_id,
            chain_id,
            ring_reference,
            signed_txs
        );
        if (!persisted) {
            return std::unexpected(persisted.error());
        }

        return signed_txs;
    }

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

        if (!req.projectors.empty()) {
            body["projectors"] = req.projectors;
        } else if (!req.projector.empty()) {
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

    std::expected<DeploySubmission, std::string>
    DeployClient::submit(
        const std::string& plan_id,
        const std::string& chain_id,
        const nlohmann::json& ring_reference,
        const nlohmann::json& signed_txs
    ) {
        const nlohmann::json body = {
            {"plan_id", plan_id},
            {"ring_reference", ring_reference},
            {"chain_id", chain_id},
            {"signed_transactions", signed_txs}
        };
        const auto serialized_body = body.dump();

        const auto attempt_count =
            kSubmitRetryDelays.size() + 1;

        for (std::size_t attempt = 0;
             attempt < attempt_count;
             ++attempt) {
            httplib::Client cli(base_url_);
            cli.set_read_timeout(60, 0);
            cli.set_connection_timeout(10, 0);

            auto res = common::ScopedTimer::measure(
                std::format(
                    "DeployClient::submit - HTTP POST {}/api/deploy/submit",
                    base_url_
                ),
                std::chrono::milliseconds(50),
                [&]() {
                    return cli.Post(
                        "/api/deploy/submit",
                        serialized_body,
                        "application/json"
                    );
                }
            );

            if (!res) {
                if (attempt + 1 == attempt_count) {
                    return std::unexpected(
                        std::format(
                            "HTTP error (submit): {}",
                            httplib::to_string(res.error())
                        )
                    );
                }

                std::this_thread::sleep_for(
                    kSubmitRetryDelays.at(attempt)
                );
                continue;
            }

            if (res->status != 202) {
                if (is_retryable_submit_status(res->status) &&
                    attempt + 1 < attempt_count) {
                    std::this_thread::sleep_for(
                        kSubmitRetryDelays.at(attempt)
                    );
                    continue;
                }

                return std::unexpected(
                    std::format(
                        "Submit failed: HTTP {} - {}",
                        res->status,
                        res->body
                    )
                );
            }

            try {
                const auto response =
                    nlohmann::json::parse(res->body);
                const auto status =
                    response.value("status", "");

                if (status != "queued" &&
                    status != "partially_queued") {
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
                        response.at("admission_error")
                            .get<std::string>();
                }

                return submission;
            } catch (const std::exception& e) {
                return std::unexpected(
                    std::string(
                        "Invalid submit response: "
                    ) + e.what()
                );
            }
        }

        return std::unexpected(
            "Submit failed without a terminal response"
        );
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

        if (!plan.contains("plan_id") ||
            !plan.at("plan_id").is_string()) {
            return std::unexpected(
                "Invalid plan: missing plan_id"
            );
        }

        if (!plan.contains("transactions") ||
            !plan.at("transactions").is_array()) {
            return std::unexpected(
                "Invalid plan: missing transactions"
            );
        }

        const auto plan_id =
            plan.at("plan_id").get<std::string>();
        const auto& transactions = plan.at("transactions");

        // A prepare that finds no structural change is already complete:
        // there is nothing to admit or finalize. Any cached submit for this
        // plan is stale from the client's point of view and can be removed.
        if (transactions.empty()) {
            if (const auto cleared =
                    clear_pending_submission(plan_id);
                !cleared) {
                LOG_THIS_WARN(
                    "Empty deploy plan completed but pending submission cache "
                    "could not be cleared: {}",
                    cleared.error()
                );
            }

            return domain::DeployResult{
                .success = true,
                .plan_id = plan_id
            };
        }

        if (!plan.contains("ring_reference") ||
            !plan.at("ring_reference").is_object()) {
            return std::unexpected(
                "Invalid plan: missing ring_reference"
            );
        }

        auto signed_txs_res =
            load_or_create_signed_transactions(
                plan_id,
                req.chain_id,
                plan.at("ring_reference"),
                transactions,
                wallet
            );

        if (!signed_txs_res) {
            return std::unexpected(signed_txs_res.error());
        }

        const auto& signed_txs = *signed_txs_res;

        auto submit_res = submit(
            plan_id,
            req.chain_id,
            plan.at("ring_reference"),
            signed_txs
        );

        if (!submit_res) {
            return std::unexpected(submit_res.error());
        }

        // A partial admission may already have changed durable network state.
        // Observe every admitted block before returning the admission error.
        if (!submit_res->pending_block_ids.empty()) {
            auto finalized = wait_for_finalization(
                req.chain_id,
                submit_res->pending_block_ids
            );

            if (!finalized) {
                return std::unexpected(finalized.error());
            }
        }

        if (submit_res->admission_error) {
            return std::unexpected(
                "Deploy was only partially queued: " +
                *submit_res->admission_error
            );
        }

        if (submit_res->pending_block_ids.size() !=
            signed_txs.size()) {
            return std::unexpected(
                std::format(
                    "Deploy queued {}/{} transactions",
                    submit_res->pending_block_ids.size(),
                    signed_txs.size()
                )
            );
        }

        if (const auto cleared =
                clear_pending_submission(plan_id);
            !cleared) {
            LOG_THIS_WARN(
                "Deploy finalized but pending submission cache could not be cleared: {}",
                cleared.error()
            );
        }

        return domain::DeployResult{
            .success = true,
            .plan_id = plan_id
        };
    }

}
