#pragma once

#include <filesystem>
#include <vector>
#include <string>
#include <unordered_set>
#include <future>
#include <mutex>

#include "AbstractCommand.hpp"
#include "PushCommand.hpp"
#include "common/Logger.hpp"
#include "common/Hash.hpp"
#include "domain/graph/Action.hpp"

namespace utx::app::use_case {

class CommitCommand final : public AbstractCommand {
public:
    explicit CommitCommand(const infrastructure::context::AppContext &ctx)
        : ctx_(ctx) {}

    [[nodiscard]]
    int execute(const std::vector<std::string> &args) override {

        if (args.size() < 3 || args[2] == "--help" || args[2] == "-h") {
            LOG_THIS_INFO(
                "{}Usage: utx commit \"message\" [--push]{}",
                utx::app::domain::color::yellow,
                utx::app::domain::color::reset);
            return 1;
        }

        if (!ctx_.wallet) {
            LOG_THIS_ERROR(
                "{}❌ No wallet configured. Please run 'utx login <wallet_path>' first.{}",
                utx::app::domain::color::red,
                utx::app::domain::color::reset);
            return 1;
        }

        bool force_snapshot = false;
        for (size_t i = 3; i < args.size(); ++i)
            if (args[i] == "--force-snapshot")
                force_snapshot = true;

        const std::string commit_message = args[2];
        const auto my_address = utx::domain::model::Address(ctx_.wallet->address);

        std::string global_revision_content;
        size_t total_chains_modified = 0;
        size_t total_blocks_emitted = 0;

        std::unordered_set<std::string> touched_paths;

        std::mutex global_mutex;

        std::vector<std::future<void>> futures;

        for (auto &target : ctx_.deploy_config.targets) {

            futures.emplace_back(std::async(std::launch::async, [&]() {

                auto deploy_client = ctx_.deploy_client();

                std::filesystem::path local_path = ctx_.root / target.path;

                if (!std::filesystem::exists(local_path) ||
                    !std::filesystem::is_regular_file(local_path))
                    return;

                if (!target.last_revision_id.empty()) {
                    LOG_THIS_WARN(
                        "  {}⚠️ {} already committed (rev:{}). Use 'utx push'.{}",
                        utx::app::domain::color::yellow,
                        target.path,
                        target.last_revision_id,
                        utx::app::domain::color::reset);
                    return;
                }

                const std::string raw_content =
                    common::io::read_file(local_path.string());

                const std::string current_hash =
                    common::md5_hex(raw_content);

                if (!target.last_synced_hash.empty() &&
                    current_hash == target.last_synced_hash)
                    return;

                domain::DeployRequest req;
                req.chain_id = target.chain;
                req.file_path = target.path;
                req.kind = to_string(target.kind);
                req.content = raw_content;
                req.commit_message = commit_message;
                req.force_snapshot = force_snapshot;

                auto plan_res = deploy_client.prepare(req, my_address.to_string());

                if (!plan_res) {
                    LOG_THIS_ERROR(
                        "  {}❌ Deploy prepare failed for {} [{}]: {}{}",
                        utx::app::domain::color::red,
                        target.path,
                        target.chain,
                        plan_res.error(),
                        utx::app::domain::color::reset);
                    return;
                }

                const auto &plan = *plan_res;

                if (!plan.contains("transactions") || !plan["transactions"].is_array()) {
                    LOG_THIS_ERROR("  ❌ Invalid plan returned by node for {}", target.path);
                    return;
                }

                const auto &txs = plan["transactions"];

                if (txs.empty()) {
                    std::lock_guard lock(global_mutex);
                    target.last_synced_hash = current_hash;
                    return;
                }

                std::string chain_segment;

                for (const auto &tx : txs) {
                    const std::string payload = tx["payload_data"].get<std::string>();
                    chain_segment += payload + "\n";
                }

                // 🔒 Critical section
                {
                    std::lock_guard lock(global_mutex);

                    touched_paths.insert(target.path);

                    LOG_THIS_INFO("  📊 {}:", target.path);
                    if (plan.contains("strategy"))
                        LOG_THIS_INFO("     Strategy: {}", plan["strategy"].get<std::string>());
                    LOG_THIS_INFO("     Transactions: {}", txs.size());

                    global_revision_content += "CHAIN:" + target.chain + "\n";
                    global_revision_content += "PLAN:" + plan["plan_id"].get<std::string>() + "\n";
                    global_revision_content += chain_segment + "\n";

                    total_blocks_emitted += static_cast<uint8_t>(txs.size());
                    total_chains_modified++;
                }
            }));
        }

        // 🔥 Wait all
        for (auto &f : futures)
            f.get();

        if (total_chains_modified == 0) {
            LOG_THIS_INFO("✨ Nothing to commit.");
            return 0;
        }

        const std::string global_rev_id =
            common::md5_hex(global_revision_content);

        std::filesystem::path rev_dir =
            ctx_.root / infrastructure::deploy::kUtxDir / "revisions";

        std::filesystem::create_directories(rev_dir);

        std::string filename =
            "rev_" + global_rev_id.substr(0, 12) + ".utx";

        std::ofstream out(rev_dir / filename);
        out << global_revision_content;

        for (auto &target : ctx_.deploy_config.targets) {
            if (touched_paths.contains(target.path))
                target.last_revision_id = global_rev_id;
        }

        std::ofstream f_deploy(
            ctx_.root / infrastructure::deploy::kDeployFile);
        f_deploy << json(ctx_.deploy_config).dump(4);

        LOG_THIS_INFO(
            "\n{}✅ Revision {} created! ({} blocks across {} chains).{}",
            utx::app::domain::color::green,
            global_rev_id,
            total_blocks_emitted,
            total_chains_modified,
            utx::app::domain::color::reset);

        bool auto_push = false;
        bool debug_flag = false;
        for (size_t i = 3; i < args.size(); ++i) {
            if (args[i] == "--push") auto_push = true;
            if (args[i] == "--debug") debug_flag = true;
        }

        if (auto_push) {
            if (debug_flag)
                return PushCommand(ctx_).execute({"--debug"});
            return PushCommand(ctx_).execute({});

        }

        LOG_THIS_INFO("Ready for push.");
        return 0;
    }

private:
    infrastructure::context::AppContext ctx_;
};

}
