#pragma once

#include <filesystem>

#include "AbstractCommand.hpp"

#include "common/Logger.hpp"
#include "common/IO.hpp"
#include "common/Hash.hpp"

#include "domain/Types.hpp"
#include "infrastructure/ignore/IgnoreManager.hpp"

namespace utx::app::use_case {
    /**
     * cmd_status : Check the status of tracked files in the Utopixia project.
     * utx status
     */
    class StatusCommand final : public AbstractCommand {
    public:
        /** Constructor
         */
        explicit StatusCommand(
            const infrastructure::context::AppContext &ctx
        )
        : ctx_(ctx) {}

        [[nodiscard]]
        int execute(const std::vector<std::string>& args) override {
            const auto target_size = ctx_.deploy_config.targets.size();
            if (target_size > 0) {
                LOG_THIS_INFO("{}🔍 Checking Utopixia status ({} targets)...{}", domain::color::bold, target_size,
                              domain::color::reset);
            }
            LOG_THIS_INFO("---------------------------------------------------------");

            std::vector<std::string> tracked_files;

            for (const auto &target: ctx_.deploy_config.targets) {
                tracked_files.push_back(target.path);
                std::filesystem::path local_p = ctx_.root / target.path;

                if (!std::filesystem::exists(local_p)) {
                    LOG_THIS_INFO(
                        "  {}[DELETED]{}    {} {}{}{} (File missing on disk)",
                        domain::color::red,
                        domain::color::reset, target.path,
                        domain::color::grey, target.chain,
                        domain::color::reset
                    );
                    continue;
                }
                std::string raw_content = common::io::read_file(local_p.string());
                std::string current_file_hash = common::md5_hex(raw_content);

                if (current_file_hash != target.last_synced_hash) {
                    LOG_THIS_INFO(
                        "  {}[MODIFIED]{}   {} {}{}{} (File changed, needs deploy)",
                        domain::color::yellow,
                        domain::color::reset,
                        target.path,
                        domain::color::grey,
                        target.chain,
                        domain::color::reset
                    );
                } else {
                    LOG_THIS_INFO(
                        "  {}[CLEAN]{}      {} {}{}{} (Synced)",
                        domain::color::green,
                        domain::color::reset,
                        target.path,
                        domain::color::grey,
                        target.chain,
                        domain::color::reset
                    );
                }
            }

            // Untracked files
            bool has_untracked = false;
            const auto ignores = ctx_.ignore_manager.load_ignore_set(ctx_.root);
            for (std::filesystem::recursive_directory_iterator it(ctx_.root), end; it != end; ++it) {
                const auto &entry = *it;

                auto rel = std::filesystem::relative(entry.path(), ctx_.root).generic_string();
                rel = infrastructure::ignore::normalize_rel_path(rel);

                if (const bool is_dir = entry.is_directory(); ignores.is_ignored(rel, is_dir)) {
                    if (is_dir) it.disable_recursion_pending(); // skip subtree
                    continue;
                }

                if (!entry.is_regular_file()) continue;

                if (std::ranges::find(tracked_files, rel) == tracked_files.end()) {
                    if (!has_untracked) {
                        LOG_THIS_INFO(
                            "\n{}Untracked files (use 'utx add <file>'):{}",
                            domain::color::bold,
                            domain::color::reset
                        );
                        has_untracked = true;
                    }
                    LOG_THIS_INFO(
                        "  {}[UNTRACKED]{} {}",
                        domain::color::red,
                        domain::color::reset,
                        rel
                    );
                }
            }

            return 0;
        }

    private:
        infrastructure::context::AppContext ctx_;
    };
}
