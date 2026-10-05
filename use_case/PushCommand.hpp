#pragma once

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "AbstractCommand.hpp"
#include "common/Hash.hpp"
#include "common/Logger.hpp"
#include "common/ThreadPool.hpp"

namespace utx::app::use_case {

class PushCommand final : public AbstractCommand {
public:
    static constexpr size_t kMaxThreads = 16;

    explicit PushCommand(const infrastructure::context::AppContext &ctx)
        : ctx_(ctx) {}

    [[nodiscard]]
    int execute(const std::vector<std::string> &args) override {

        if (!ctx_.wallet) {
            LOG_THIS_ERROR("{}❌ No wallet configured.{}", domain::color::red, domain::color::reset);
            return 1;
        }

        std::string rev_id;
        for (const auto &t : ctx_.deploy_config.targets) {
            if (!t.last_revision_id.empty()) {
                rev_id = t.last_revision_id;
                break;
            }
        }

        if (rev_id.empty()) {
            if (deploy_manifest("manifest_update") != 0) {
                return 1;
            }
            LOG_THIS_WARN("{}ℹ️ Nothing to push.{}", domain::color::cyan, domain::color::reset);
            return 0;
        }

        const std::filesystem::path rev_path =
            ctx_.root / infrastructure::deploy::kUtxDir / "revisions" /
            ("rev_" + rev_id.substr(0, 12) + ".utx");

        if (!std::filesystem::exists(rev_path)) {
            LOG_THIS_ERROR("{}❌ Revision file not found{}", domain::color::red, domain::color::reset);
            return 1;
        }

        struct ChainWork {
            std::string plan_id;
            nlohmann::json ring_reference;
            std::string content_hash;
            std::vector<std::string> payloads;
        };

        std::unordered_map<std::string, ChainWork> work_map;

        std::ifstream infile(rev_path);
        std::string line;
        std::string current_chain;

        while (std::getline(infile, line)) {
            if (line.empty()) {
                continue;
            }

            if (line.starts_with("CHAIN:")) {
                current_chain = line.substr(6);
                continue;
            }

            if (current_chain.empty()) {
                LOG_THIS_ERROR("❌ Malformed revision file: metadata without CHAIN");
                return 1;
            }

            auto &entry = work_map[current_chain];

            if (line.starts_with("PLAN:")) {
                entry.plan_id = line.substr(5);
                continue;
            }

            if (line.starts_with("RING:")) {
                try {
                    entry.ring_reference = nlohmann::json::parse(line.substr(5));
                } catch (const std::exception &e) {
                    LOG_THIS_ERROR("❌ Invalid ring reference for chain {}: {}", current_chain, e.what());
                    return 1;
                }
                continue;
            }

            if (line.starts_with("HASH:")) {
                entry.content_hash = line.substr(5);
                continue;
            }

            entry.payloads.push_back(line);
        }

        for (const auto &[chain_id, work] : work_map) {
            if (work.plan_id.empty() ||
                !work.ring_reference.is_object() ||
                work.content_hash.empty() ||
                work.payloads.empty()) {
                LOG_THIS_ERROR(
                    "❌ Revision for chain {} is missing async deploy metadata. "
                    "Re-create the revision with the current utx version.",
                    chain_id
                );
                return 1;
            }
        }

        std::mutex config_mutex;
        std::mutex success_mutex;
        size_t success = 0;

        utx::common::ThreadPool pool(kMaxThreads);
        std::vector<std::future<void>> futures;

        for (const auto &[chain_id, work] : work_map) {
            futures.emplace_back(
                pool.enqueue([&, chain_id, work]() {
                    auto local_client = ctx_.deploy_client();

                    const auto target_it = std::find_if(
                        ctx_.deploy_config.targets.begin(),
                        ctx_.deploy_config.targets.end(),
                        [&](const auto &target) {
                            return target.chain == chain_id;
                        }
                    );

                    if (target_it == ctx_.deploy_config.targets.end()) {
                        LOG_THIS_ERROR("❌ No deploy target found for chain {}", chain_id);
                        return;
                    }

                    const auto target = *target_it;
                    const auto local_path = ctx_.root / target.path;

                    if (!std::filesystem::exists(local_path) ||
                        !std::filesystem::is_regular_file(local_path)) {
                        LOG_THIS_ERROR(
                            "❌ Cannot repush {}: local file {} is missing",
                            chain_id,
                            target.path
                        );
                        return;
                    }

                    const auto raw_content =
                        common::io::read_file(local_path.string());
                    const auto current_hash =
                        common::md5_hex(raw_content);

                    if (current_hash != work.content_hash) {
                        LOG_THIS_ERROR(
                            "❌ Cannot repush {}: {} changed after commit. "
                            "Create a new revision first.",
                            chain_id,
                            target.path
                        );
                        return;
                    }

                    const auto needs_reprepare = [](const std::string &error) {
                        return error.find("plan_not_found") != std::string::npos ||
                               error.find("stale_plan") != std::string::npos ||
                               error.find("plan_ring_reference_mismatch") != std::string::npos ||
                               error.find("wrong_deploy_gate") != std::string::npos;
                    };

                    const auto submit_payloads =
                        [&](const std::string &plan_id,
                            const nlohmann::json &ring_reference,
                            const std::vector<std::string> &payloads)
                            -> std::expected<void, std::string> {

                        if (payloads.empty()) {
                            return {};
                        }

                        nlohmann::json signed_txs = nlohmann::json::array();
                        size_t payload_size = 0;

                        for (const auto &payload : payloads) {
                            auto signed_res = local_client.build_signed_tx(
                                payload,
                                chain_id,
                                *ctx_.wallet
                            );

                            if (!signed_res) {
                                return std::unexpected(
                                    std::format(
                                        "Signing failed for {}: {}",
                                        chain_id,
                                        signed_res.error()
                                    )
                                );
                            }

                            signed_txs.push_back(std::move(signed_res.value()));
                            payload_size += payload.size();
                        }

                        auto submit_res = local_client.submit(
                            plan_id,
                            chain_id,
                            ring_reference,
                            signed_txs
                        );

                        if (!submit_res) {
                            return std::unexpected(submit_res.error());
                        }

                        LOG_THIS_INFO(
                            "⏳ Deploy queued for chain {}. Plan ID: {}, Transactions: {}, Payload size: {} bytes",
                            chain_id,
                            plan_id,
                            submit_res->pending_block_ids.size(),
                            payload_size
                        );

                        if (!submit_res->pending_block_ids.empty()) {
                            auto finalized = local_client.wait_for_finalization(
                                chain_id,
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

                        if (submit_res->pending_block_ids.size() != signed_txs.size()) {
                            return std::unexpected(
                                std::format(
                                    "Deploy queued {}/{} transactions",
                                    submit_res->pending_block_ids.size(),
                                    signed_txs.size()
                                )
                            );
                        }

                        return {};
                    };

                    auto result = submit_payloads(
                        work.plan_id,
                        work.ring_reference,
                        work.payloads
                    );

                    if (!result && needs_reprepare(result.error())) {
                        LOG_THIS_WARN(
                            "⚠️ Deploy plan for {} is no longer usable; preparing a fresh plan.",
                            chain_id
                        );

                        domain::DeployRequest req;
                        req.chain_id = chain_id;
                        req.file_path = target.path;
                        req.kind = to_string(target.kind);
                        req.content = raw_content;
                        req.commit_message = "Repush revision " + rev_id;

                        const auto chain_exists =
                            ctx_.network_client.get_last_block(
                                utx::domain::model::Address(chain_id)
                            ).has_value();
                        if (!chain_exists) {
                            req.projectors =
                                target.genesis_projectors.empty()
                                    ? domain::compose_genesis_projectors(target.kind)
                                    : target.genesis_projectors;
                        }

                        auto plan_res = local_client.prepare(
                            req,
                            ctx_.wallet->address
                        );

                        if (!plan_res) {
                            LOG_THIS_ERROR(
                                "❌ Re-prepare failed for {}: {}",
                                chain_id,
                                plan_res.error()
                            );
                            return;
                        }

                        const auto &plan = *plan_res;

                        if (!plan.contains("content_hash") ||
                            !plan.at("content_hash").is_string() ||
                            plan.at("content_hash").get<std::string>() != work.content_hash) {
                            LOG_THIS_ERROR(
                                "❌ Fresh plan content hash mismatch for {}",
                                chain_id
                            );
                            return;
                        }

                        if (!plan.contains("transactions") ||
                            !plan.at("transactions").is_array()) {
                            LOG_THIS_ERROR(
                                "❌ Fresh plan for {} has no transactions array",
                                chain_id
                            );
                            return;
                        }

                        std::vector<std::string> fresh_payloads;
                        for (const auto &tx : plan.at("transactions")) {
                            fresh_payloads.push_back(
                                tx.at("payload_data").get<std::string>()
                            );
                        }

                        if (fresh_payloads.empty()) {
                            LOG_THIS_INFO(
                                "✅ {} already matches revision {} on-chain.",
                                target.path,
                                rev_id
                            );
                            result = {};
                        } else {
                            if (!plan.contains("ring_reference") ||
                                !plan.at("ring_reference").is_object()) {
                                LOG_THIS_ERROR(
                                    "❌ Fresh plan for {} has no ring_reference",
                                    chain_id
                                );
                                return;
                            }

                            result = submit_payloads(
                                plan.at("plan_id").get<std::string>(),
                                plan.at("ring_reference"),
                                fresh_payloads
                            );
                        }
                    }

                    if (!result) {
                        LOG_THIS_ERROR(
                            "❌ Deploy did not finalize for {}: {}",
                            chain_id,
                            result.error()
                        );
                        return;
                    }

                    LOG_THIS_INFO(
                        "✅ Deploy finalized for chain {}",
                        chain_id
                    );

                    {
                        std::lock_guard<std::mutex> lock(config_mutex);

                        for (auto &t : ctx_.deploy_config.targets) {
                            if (t.chain == chain_id) {
                                t.last_synced_hash = work.content_hash;
                                t.last_revision_id.clear();
                            }
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lock(success_mutex);
                        ++success;
                    }
                })
            );
        }

        for (auto &f : futures) {
            f.get();
        }

        const auto saved =
            infrastructure::deploy::DeployConfigManager::save_deploy_config_atomic(
                ctx_.root,
                ctx_.deploy_config
            );

        if (!saved) {
            LOG_THIS_ERROR(
                "{}❌ Failed to persist deploy config: {}{}",
                domain::color::red,
                saved.error(),
                domain::color::reset
            );
            return 1;
        }

        LOG_THIS_INFO(
            "🎯 Push finalized: {}/{} chains",
            success,
            work_map.size()
        );

        if (success != work_map.size()) {
            LOG_THIS_WARN(
                "{}⚠️ Revision remains pending. Re-run 'utx push' after fixing the reported error.{}",
                domain::color::yellow,
                domain::color::reset
            );
            return 1;
        }

        if (deploy_manifest(rev_id) != 0) {
            return 1;
        }

        LOG_THIS_INFO(
            "{}🎯 Manifest update finalized.{}",
            domain::color::green,
            domain::color::reset
        );

        return 0;
    }

    int deploy_manifest(const std::string &rev_id) {
        auto deploy_client = ctx_.deploy_client();
        const auto deploy_chain_address = ctx_.project_config.deploy_chain;

        if (deploy_chain_address.empty()) {
            return 0;
        }

        LOG_THIS_INFO(
            "{}🚀 Deploying manifest update on chain {}...{}",
            domain::color::green,
            deploy_chain_address,
            domain::color::reset
        );

        try {
            const auto deploy_json =
                utx::common::io::read_file(
                    (ctx_.root / infrastructure::deploy::kDeployFile).string()
                );

            auto g = ctx_.graph_parser.make_deploy_graph(
                deploy_chain_address,
                deploy_json
            );

            domain::DeployRequest req;
            req.chain_id = deploy_chain_address;
            req.file_path = infrastructure::deploy::kDeployFile;
            req.content = g->to_json_string();
            req.commit_message =
                "Update deploy manifest on revision " + rev_id;

            auto deploy_res = deploy_client.deploy(
                req,
                *ctx_.wallet
            );

            if (!deploy_res) {
                LOG_THIS_ERROR(
                    "{}❌ Deploy manifest failed: {}{}",
                    domain::color::red,
                    deploy_res.error(),
                    domain::color::reset
                );
                return 1;
            }

            LOG_THIS_INFO(
                "{}✅ Deploy manifest finalized on-chain.{}",
                domain::color::green,
                domain::color::reset
            );

            return 0;
        } catch (const std::exception &e) {
            LOG_THIS_ERROR(
                "{}❌ Manifest push error: {}{}",
                domain::color::red,
                e.what(),
                domain::color::reset
            );
            return 1;
        }
    }

private:
    infrastructure::context::AppContext ctx_;
};

}
