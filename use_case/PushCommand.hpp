#pragma once

#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>
#include <future>
#include <mutex>

#include "AbstractCommand.hpp"
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
        for (const auto &t: ctx_.deploy_config.targets) {
            if (!t.last_revision_id.empty()) {
                rev_id = t.last_revision_id;
                break;
            }
        }

        if (rev_id.empty()) {
            deploy_manifest("manifest_update");
            LOG_THIS_WARN("{}ℹ️ Nothing to push.{}", domain::color::cyan, domain::color::reset);
            return 0;
        }

        std::filesystem::path rev_path =
            ctx_.root / infrastructure::deploy::kUtxDir / "revisions" /
            ("rev_" + rev_id.substr(0, 12) + ".utx");

        if (!std::filesystem::exists(rev_path)) {
            LOG_THIS_ERROR("{}❌ Revision file not found{}", domain::color::red, domain::color::reset);
            return 1;
        }

        struct ChainWork {
            std::string plan_id;
            std::vector<std::string> payloads;
        };

        std::unordered_map<std::string, ChainWork> work_map;

        std::ifstream infile(rev_path);
        std::string line;

        std::string current_chain;
        std::string current_plan;

        while (std::getline(infile, line)) {
            if (line.empty()) continue;

            if (line.starts_with("CHAIN:")) {
                current_chain = line.substr(6);
                continue;
            }

            if (line.starts_with("PLAN:")) {
                current_plan = line.substr(5);
                continue;
            }

            if (current_chain.empty() || current_plan.empty()) {
                LOG_THIS_ERROR("❌ Malformed revision file");
                return 1;
            }

            auto &entry = work_map[current_chain];
            entry.plan_id = current_plan;
            entry.payloads.push_back(line);
        }

        std::mutex config_mutex;
        std::mutex success_mutex;

        size_t success = 0;

        utx::common::ThreadPool pool(kMaxThreads);
        std::vector<std::future<void>> futures;

        for (auto &[chain_id, work] : work_map) {

            futures.emplace_back(
                pool.enqueue([&, chain_id, &work]() {

                    auto local_client = ctx_.deploy_client();

                    nlohmann::json signed_txs = nlohmann::json::array();

                    size_t payload_size = 0;
                    for (const auto &payload : work.payloads) {
                        auto signed_res =
                            local_client.build_signed_tx(payload, *ctx_.wallet);

                        if (!signed_res) {
                            LOG_THIS_ERROR("❌ Signing failed ({}): {}", chain_id, signed_res.error());
                            return;
                        }

                        signed_txs.push_back(std::move(signed_res.value()));
                        payload_size += payload.size();
                    }

                    auto submit_res =
                        local_client.submit(work.plan_id, chain_id, signed_txs);

                    if (!submit_res) {
                        LOG_THIS_ERROR("❌ Submit failed for {}: {}", chain_id, submit_res.error());
                        return;
                    }


                    LOG_THIS_INFO("✅ Deploy successful for chain {}. Plan ID: {}, Payload size: {} bytes",
                                  chain_id, work.plan_id, payload_size);

                    {
                        std::lock_guard<std::mutex> lock(config_mutex);

                        for (auto &t : ctx_.deploy_config.targets) {
                            if (t.chain == chain_id) {
                                const std::string content =
                                    common::io::read_file((ctx_.root / t.path).string());

                                t.last_synced_hash = common::md5_hex(content);
                                t.last_revision_id.clear();
                            }
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lock(success_mutex);
                        success++;
                    }
                })
            );
        }

        // attendre toutes les tâches
        for (auto &f : futures) {
            f.get();
        }

        infrastructure::deploy::DeployConfigManager::save_deploy_config_atomic(
            ctx_.root,
            ctx_.deploy_config
        );

        LOG_THIS_INFO("🎯 Push complete: {}/{} chains", success, work_map.size());

        if (deploy_manifest(rev_id)) {
            LOG_THIS_INFO("🎯 Manifest update complete.{}", domain::color::green, domain::color::reset);
        }

        return success == work_map.size() ? 0 : 1;
    }

    int deploy_manifest(const std::string &rev_id) {
        auto deploy_client = ctx_.deploy_client();
        const auto deploy_chain_address = ctx_.project_config.deploy_chain;

        if (deploy_chain_address.empty()) {
            return 0;
        }

        LOG_THIS_INFO("{}🚀 Deploying manifest update on chain {}...{}",
                      domain::color::green,
                      deploy_chain_address,
                      domain::color::reset);

        try {
            const auto deploy_json =
                utx::common::io::read_file((ctx_.root / infrastructure::deploy::kDeployFile).string());

            auto g = ctx_.graph_parser.make_deploy_graph(deploy_chain_address, deploy_json);

            domain::DeployRequest req;
            req.chain_id = deploy_chain_address;
            req.file_path = infrastructure::deploy::kDeployFile;
            req.content = g->to_json_string();
            req.commit_message = "Update deploy manifest on revision " + rev_id;

            auto deploy_res = deploy_client.deploy(req, *ctx_.wallet);

            if (!deploy_res) {
                LOG_THIS_ERROR("{}❌ Deploy manifest prepare failed: {}{}",
                               domain::color::red,
                               deploy_res.error(),
                               domain::color::reset);
                return 1;
            }

            LOG_THIS_INFO("{}✅ Deploy manifest updated on-chain.{}",
                          domain::color::green,
                          domain::color::reset);

        } catch (const std::exception &e) {
            LOG_THIS_ERROR("{}❌ Manifest push error: {}{}",
                           domain::color::red,
                           e.what(),
                           domain::color::reset);
        }

        return 0;
    }

private:
    infrastructure::context::AppContext ctx_;
};

}
