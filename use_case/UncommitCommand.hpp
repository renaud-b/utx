#pragma once

#include "AbstractCommand.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"

namespace utx::app::use_case {

/**
 * cmd_uncommit : Cancel a pending commit by removing the revision file
 * and cleaning up the DeployConfig.
 *
 * Options:
 *   --reset : also clears last_synced_hash for all targets
 */
class UncommitCommand final : public AbstractCommand {
public:
    explicit UncommitCommand(
        const infrastructure::context::AppContext &ctx
    ) : ctx_(ctx) {}

    int execute(const std::vector<std::string> &args) override {

        // --- Parse flags ---
        bool reset_synced = false;
        for (const auto& arg : args) {
            if (arg == "--reset") {
                reset_synced = true;
            }
        }

        std::string target_rev_id;

        // Search for the revision ID to uncommit
        for (const auto &target: ctx_.deploy_config.targets) {
            if (!target.last_revision_id.empty()) {
                target_rev_id = target.last_revision_id;
                break;
            }
        }

        if (target_rev_id.empty()) {
            LOG_THIS_INFO(
                "{}ℹ️ No pending commit to cancel.{}",
                domain::color::cyan,
                domain::color::reset
            );

            // Even without commit, allow reset if requested
            if (!reset_synced) {
                return 0;
            }
        }

        if (!target_rev_id.empty()) {
            LOG_THIS_INFO("Uncommitting revision {}...", target_rev_id);

            // Delete revision file
            std::filesystem::path rev_file =
                ctx_.root / infrastructure::deploy::kUtxDir / "revisions" /
                ("rev_" + target_rev_id.substr(0, 12) + ".utx");

            if (std::filesystem::exists(rev_file)) {
                try {
                    std::filesystem::remove(rev_file);
                    LOG_THIS_INFO(
                        "  🗑️  Deleted: .utx/revisions/{}",
                        rev_file.filename().string()
                    );
                } catch (const std::exception &e) {
                    LOG_THIS_ERROR(
                        "  {}❌ Error deleting revision file: {}",
                        domain::color::red,
                        e.what()
                    );
                }
            }
        }

        // --- Clean DeployConfig ---
        size_t cleaned_count = 0;

        for (auto &target: ctx_.deploy_config.targets) {
            target.last_revision_id = "";

            if (reset_synced) {
                target.last_synced_hash = "";
            }

            cleaned_count++;
        }

        // Save updated DeployConfig
        try {
            std::ofstream f(ctx_.root / infrastructure::deploy::kDeployFile);
            f << json(ctx_.deploy_config).dump(4);

            LOG_THIS_INFO(
                "\n{}✅ Successfully uncommitted {} targets.{}",
                domain::color::green,
                cleaned_count,
                domain::color::reset
            );

            if (reset_synced) {
                LOG_THIS_WARN(
                    "{}⚠️ Sync state reset: all targets are now considered unsynced.{}",
                    domain::color::yellow,
                    domain::color::reset
                );
            }

            LOG_THIS_INFO(
                "Files are back to 'Modified' state (not staged for push)."
            );

        } catch (const std::exception &e) {
            LOG_THIS_ERROR(
                "{}❌ Failed to update deploy manifest: {}",
                domain::color::red,
                e.what()
            );
            return 1;
        }

        return 0;
    }

private:
    infrastructure::context::AppContext ctx_;
};

}
