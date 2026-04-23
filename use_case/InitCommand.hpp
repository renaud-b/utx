#pragma once

#include <filesystem>
#include "AbstractCommand.hpp"
#include "common/Logger.hpp"

#include "domain/Types.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"

namespace utx::app::use_case {
    /** Marker interface for command objects in the application domain. */
    class InitCommand final : public AbstractCommand {
    public:
        /** Constructor
         */
        explicit InitCommand() = default;

        [[nodiscard]]
        int execute(const std::vector<std::string>& args) override {
            auto root = std::filesystem::current_path();
            auto deploy_p = root / infrastructure::deploy::kDeployFile;

            if (std::filesystem::exists(deploy_p)) {
                LOG_THIS_INFO("ℹ️ Utopixia project already initialized (found {}).", infrastructure::deploy::kDeployFile);
                return 0;
            }

            domain::DeployConfig cfg;
            std::ofstream f(deploy_p);
            f << json(cfg).dump(4);

            // Initialise aussi le dossier local pour la config utilisateur
            infrastructure::deploy::save_local_config(root, domain::ProjectConfig{});

            LOG_THIS_INFO("✅ Utopixia projects has been initialized.");
            LOG_THIS_INFO("   - Deploy manifest: {}", infrastructure::deploy::kDeployFile);
            LOG_THIS_INFO("   - Local config: {}/config.json (should be gitignore)", infrastructure::deploy::kUtxDir);
            return 0;
        }

    };
}
