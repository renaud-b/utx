#pragma once

#include <utility>

#include "AbstractCommand.hpp"
#include "common/Logger.hpp"
#include "infrastructure/context/AppContext.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"

namespace utx::app::use_case {

class ResetCommand final : public AbstractCommand {
public:
    explicit ResetCommand(
        infrastructure::context::AppContext ctx
    ) : ctx_(std::move(ctx)) {}

    [[nodiscard]]
    int execute(const std::vector<std::string>& args) override {
        if (args.size() >= 3 &&
            (args[2] == "--help" || args[2] == "-h")) {
            LOG_THIS_INFO("Usage: utx reset");
            LOG_THIS_INFO(
                "  Forget local synchronization state while preserving project topology."
            );
            return 0;
        }

        size_t invalidated = 0;
        for (auto& target : ctx_.deploy_config.targets) {
            if (target.last_synced_hash.empty()) {
                continue;
            }

            target.last_synced_hash.clear();
            ++invalidated;
        }

        const auto saved =
            infrastructure::deploy::DeployConfigManager::save_deploy_config_atomic(
                ctx_.root,
                ctx_.deploy_config
            );

        if (!saved) {
            LOG_THIS_ERROR(
                "{}❌ Failed to reset local synchronization state: {}{}",
                domain::color::red,
                saved.error(),
                domain::color::reset
            );
            return 1;
        }

        if (invalidated == 0) {
            LOG_THIS_INFO("♻️ Local synchronization state was already reset.");
        } else {
            LOG_THIS_INFO(
                "♻️ Local synchronization state reset: {} tracked target(s) marked for redeployment.",
                invalidated
            );
        }

        LOG_THIS_INFO(
            "   Files, chain IDs, wallet and project configuration were preserved."
        );
        return 0;
    }

private:
    infrastructure::context::AppContext ctx_;
};

} // namespace utx::app::use_case
