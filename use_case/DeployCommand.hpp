#pragma once

#include <atomic>
#include <filesystem>
#include <future>
#include <mutex>
#include <string>
#include <vector>

#include "AbstractCommand.hpp"
#include "common/Hash.hpp"
#include "common/IO.hpp"
#include "common/Logger.hpp"
#include "common/ThreadPool.hpp"
#include "infrastructure/context/AppContext.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"

namespace utx::app::use_case {

class DeployCommand final : public AbstractCommand {
public:
    static constexpr size_t kMaxThreads = 16;

    explicit DeployCommand(infrastructure::context::AppContext ctx)
        : ctx_(std::move(ctx)) {}

    [[nodiscard]]
    int execute(const std::vector<std::string>& args) override {
        if (args.size() < 3 || args[2] == "--help" || args[2] == "-h") {
            LOG_THIS_INFO(
                "{}Usage: utx deploy \"message\" [--force-snapshot]{}",
                domain::color::yellow,
                domain::color::reset
            );
            return 1;
        }

        if (!ctx_.wallet) {
            LOG_THIS_ERROR(
                "{}❌ No wallet configured. Please run 'utx login <wallet_path>' first.{}",
                domain::color::red,
                domain::color::reset
            );
            return 1;
        }

        bool force_snapshot = false;
        for (size_t i = 3; i < args.size(); ++i) {
            if (args[i] == "--force-snapshot") {
                force_snapshot = true;
            }
        }

        const std::string commit_message = args[2];

        for (const auto& target : ctx_.deploy_config.targets) {
            if (target.kind == domain::TargetKind::Go) {
                LOG_THIS_ERROR(
                    "❌ Cannot deploy {}: Go deploys are not supported by the current V1 node deploy planner.",
                    target.path
                );
                return 1;
            }

            if (target.last_synced_hash.empty() && !target.genesis_labels.empty()) {
                LOG_THIS_ERROR(
                    "❌ Cannot create chain for {} with genesis labels: the current V1 deploy protocol does not carry labels.",
                    target.path
                );
                return 1;
            }
        }

        struct Work {
            domain::DeployTarget target;
            std::string content;
            std::string content_hash;
        };

        std::vector<Work> work;
        work.reserve(ctx_.deploy_config.targets.size());

        for (const auto& target : ctx_.deploy_config.targets) {
            const auto local_path = ctx_.root / target.path;
            if (!std::filesystem::exists(local_path) ||
                !std::filesystem::is_regular_file(local_path)) {
                continue;
            }

            const auto content = common::io::read_file(local_path.string());
            const auto content_hash = common::md5_hex(content);

            if (!target.last_synced_hash.empty() &&
                content_hash == target.last_synced_hash) {
                continue;
            }

            work.push_back(Work{
                .target = target,
                .content = content,
                .content_hash = content_hash
            });
        }

        if (work.empty()) {
            LOG_THIS_INFO("✨ Nothing to deploy.");
            return 0;
        }

        LOG_THIS_INFO(
            "🚀 Deploying {} modified chain(s) with up to {} workers...",
            work.size(),
            std::min(kMaxThreads, work.size())
        );

        std::mutex config_mutex;
        std::atomic<size_t> succeeded{0};
        std::atomic<size_t> failed{0};

        const size_t worker_count = std::max<size_t>(
            1,
            std::min(kMaxThreads, work.size())
        );

        common::ThreadPool pool(worker_count);
        std::vector<std::future<void>> futures;
        futures.reserve(work.size());

        for (const auto& item : work) {
            futures.emplace_back(
                pool.enqueue([&, item]() {
                    auto client = ctx_.deploy_client();

                    domain::DeployRequest request;
                    request.chain_id = item.target.chain;
                    request.file_path = item.target.path;
                    request.kind = to_string(item.target.kind);
                    request.content = item.content;
                    request.commit_message = commit_message;
                    request.force_snapshot = force_snapshot;

                    LOG_THIS_INFO(
                        "  ⏳ Deploying {} -> {}",
                        item.target.path,
                        item.target.chain
                    );

                    const auto result = client.deploy(request, *ctx_.wallet);
                    if (!result) {
                        LOG_THIS_ERROR(
                            "  ❌ Deploy failed for {} [{}]: {}",
                            item.target.path,
                            item.target.chain,
                            result.error()
                        );
                        ++failed;
                        return;
                    }

                    {
                        std::lock_guard lock(config_mutex);
                        for (auto& target : ctx_.deploy_config.targets) {
                            if (target.path == item.target.path &&
                                target.chain == item.target.chain) {
                                target.last_synced_hash = item.content_hash;
                            }
                        }
                    }

                    ++succeeded;
                    LOG_THIS_INFO(
                        "  ✅ Finalized {} [{}]",
                        item.target.path,
                        item.target.chain
                    );
                })
            );
        }

        for (auto& future : futures) {
            future.get();
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
            "🎯 Deploy complete: {}/{} chain(s) finalized.",
            succeeded.load(),
            work.size()
        );

        if (failed.load() != 0) {
            LOG_THIS_WARN(
                "{}⚠️ {} chain(s) failed and remain modified locally. Re-run 'utx deploy' after fixing the reported error.{}",
                domain::color::yellow,
                failed.load(),
                domain::color::reset
            );
            return 1;
        }

        return 0;
    }

private:
    infrastructure::context::AppContext ctx_;
};

} // namespace utx::app::use_case
