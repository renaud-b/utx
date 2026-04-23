#pragma once

#include <filesystem>
#include <optional>

#include "domain/Types.hpp"
#include "infrastructure/chain/NetworkClient.hpp"
#include "infrastructure/chain/TxManager.hpp"
#include "infrastructure/parser/GraphParser.hpp"
#include "infrastructure/ignore/IgnoreManager.hpp"
#include "infrastructure/wallet/WalletHelper.hpp"
#include "infrastructure/deploy_config/DeployClient.hpp"

namespace utx::app::infrastructure::context {

    static constexpr const char *kDeployFile = ".utx.deploy.json";
    static constexpr const char *kUtxDir = ".utx";
    static constexpr const char *kProjectConfigFile = ".utx/config.json";

    struct AppContext {
        std::filesystem::path root;

        domain::ProjectConfig project_config;
        domain::DeployConfig deploy_config;

        std::optional<infra::wallet::KeyPair> wallet;

        // services
        chain::NetworkClient network_client;
        chain::TxManager tx_manager;
        parser::GraphParser graph_parser;
        ignore::IgnoreManager ignore_manager;

        // helpers
        std::string project_label;

        void save_local_config() {
            std::filesystem::create_directories(root / kUtxDir);
            std::ofstream f(root / kProjectConfigFile);
            f << json(project_config).dump(4);
        }

        [[nodiscard]]
        deploy::DeployClient deploy_client() const {
            return deploy::DeployClient(project_config.api_target);
        }
    };


    class AppContextBuilder {
    public:
        static std::expected<AppContext, std::string> build(const std::vector<std::string>& args) {
            AppContext ctx;

            deploy::DeployConfigManager dc_manager;

            ctx.root = deploy::DeployConfigManager::find_repo_root(std::filesystem::current_path())
                          .value_or(std::filesystem::current_path());

            ctx.project_config = dc_manager.load_local_config(ctx.root);
            ctx.project_config = ensure_deploy_chain(ctx.project_config, ctx.root);

            auto deploy_res = dc_manager.load_deploy_config(ctx.root);
            if (!deploy_res) return std::unexpected(deploy_res.error());
            ctx.deploy_config = deploy_res.value();

            if (auto wallet_res = deploy::DeployConfigManager::load_wallet_from_config(ctx.project_config))
                ctx.wallet = wallet_res.value();

            ctx.network_client = chain::NetworkClient{ctx.project_config.api_target};
            ctx.tx_manager = chain::TxManager{};
            ctx.graph_parser = parser::GraphParser{};
            ctx.ignore_manager = ignore::IgnoreManager{};

            ctx.project_label = deploy::DeployConfigManager::project_label_from_root(ctx.root);


            return ctx;
        }

        static domain::ProjectConfig ensure_deploy_chain(
            domain::ProjectConfig pcfg,
            const std::filesystem::path &root)
        {
            if (pcfg.deploy_chain.empty()) {
                pcfg.deploy_chain = common::generate_uuid_v7().to_string();
                deploy::save_local_config(root, pcfg);

                LOG_THIS_INFO("{}🧬 Deploy chain was missing. Generated: {}{}",
                              utx::app::domain::color::cyan, pcfg.deploy_chain, utx::app::domain::color::reset);
                LOG_THIS_INFO("   (stored in {}/config.json)", utx::app::infrastructure::deploy::kUtxDir);
            }
            return pcfg;
        }
    };
}
