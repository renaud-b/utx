#pragma once

#include <filesystem>
#include "AbstractCommand.hpp"
#include "common/Logger.hpp"

#include "domain/Types.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"
#include "infrastructure/chain/NetworkClient.hpp"

namespace utx::app::use_case {
    /** cmd_login : Log in to a wallet and set it as the active session.
     * utx login <wallet_path> [--target <api>]
     */
    class LoginCommand final : public AbstractCommand {
    public:
        /** Constructor
         */
        explicit LoginCommand(const infrastructure::context::AppContext &ctx)
        : ctx_(ctx) {}

        [[nodiscard]]
        int execute(const std::vector<std::string>& args) override {
            if (args.size() < 3) {
                LOG_THIS_INFO("Usage: utx login <wallet_path> [--target <api>]");
                LOG_THIS_INFO("--target <api> : Specify the Singularity API target (default: 127.0.0.1:8080)");
                return 1;
            }

            std::string wallet_p = args[2];
            std::string target = "127.0.0.1:8080";
            for (size_t i = 3; i < args.size(); ++i) {
                if (args[i] == "--target" && i + 1 < args.size()) target = args[++i];
            }

            if (!std::filesystem::exists(wallet_p)) {
                LOG_THIS_ERROR("❌ Wallet file '{}' not found.", wallet_p);
                return 1;
            }

            infra::wallet::KeyPair wallet;
            try {
                wallet = infra::wallet::WalletHelper::load_from_file(wallet_p);
            } catch (const std::exception &e) {
                LOG_THIS_ERROR("❌ Failed to load wallet from '{}': {}", wallet_p, e.what());
                return 1;
            }


            const infrastructure::chain::NetworkClient net{target};
            if (const auto graph = net.fetch_graph_state(wallet.address); !graph) {
                LOG_THIS_WARN("⚠️ Wallet loaded but no identity found on chain. (Did you use create-identity?)");
            } else {
                LOG_THIS_INFO("🌐 Welcome back, {}!", graph->root()->get_property("user.pseudo"));
            }

            auto cfg = ctx_.project_config;
            cfg.wallet_path = std::filesystem::absolute(wallet_p).string();
            cfg.api_target = target;
            infrastructure::deploy::save_local_config(ctx_.root, cfg);

            LOG_THIS_INFO("{}✅ Session active for {}{}", utx::app::domain::color::green, wallet.address,
                          utx::app::domain::color::reset);
            return 0;
        }

    private:
        infrastructure::context::AppContext ctx_;

    };
}
