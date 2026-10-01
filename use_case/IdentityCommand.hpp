#pragma once

#include <expected>
#include <filesystem>
#include <fstream>
#include "AbstractCommand.hpp"
#include "common/Logger.hpp"

#include "domain/Types.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"
#include "infrastructure/chain/NetworkClient.hpp"
#include "infrastructure/chain/TxManager.hpp"

namespace utx::app::use_case {
    /** Marker interface for command objects in the application domain. */
    class IdentityCommand : public AbstractCommand {
    public:
        /** Constructor
         */
        explicit IdentityCommand(
            const infrastructure::context::AppContext &ctx)
            : ctx_(ctx) {
        }

        [[nodiscard]]
        int execute(const std::vector<std::string> &args) override {
            // Usage: utx identity <subcommand> [options]
            if (args.size() < 3) {
                LOG_THIS_INFO("Usage: utx identity <subcommand> [options]");
                LOG_THIS_INFO("Subcommands:");
                LOG_THIS_INFO("  create <wallet_path> <pseudo> [--target <api>]  Create a new identity");
                LOG_THIS_INFO("  show                                            Show identity info");
                LOG_THIS_INFO(
                    "  b64                                             Output base64-encoded identity config (for use in env vars)");
                return 1;
            }

            const std::string &subcmd = args[2];
            if (subcmd == "create") {
                return cmd_create_identity(ctx_.project_config, args);
            }
            if (subcmd == "b64") {
                if (!ctx_.wallet) {
                    LOG_THIS_ERROR(
                        "{}❌ No wallet configured. Please run 'utx login <wallet_path>' first.{}",
                        utx::app::domain::color::red,
                        utx::app::domain::color::reset
                    );
                    return 1;
                }
                const auto b64 = identity_config_to_b64(ctx_.wallet.value(), ctx_.network_client.target);
                LOG_THIS_INFO("{}", b64);
                return 0;
            }
            if (subcmd == "show") {
                const auto wallet = infrastructure::deploy::DeployConfigManager::load_wallet_from_config(
                    ctx_.project_config);
                if (!wallet) {
                    LOG_THIS_ERROR("{}❌ {}", domain::color::red, wallet.error());
                    return 1;
                }

                const auto user_profile = ctx_.network_client.fetch_graph_state(wallet->address);
                LOG_THIS_INFO("{} Identity Info{}", utx::app::domain::color::bold, utx::app::domain::color::reset);
                LOG_THIS_INFO("  Address : {}", wallet->address);
                if (!user_profile) {
                    LOG_THIS_ERROR("{}❌ Failed to fetch identity graph: {}{}", utx::app::domain::color::red,
                                   user_profile.error(), utx::app::domain::color::reset);
                    return 0;
                }
                const auto pseudo = user_profile->root()
                                        ? user_profile->root()->get_property("user.pseudo")
                                        : "<unknown>";
                LOG_THIS_INFO("  Pseudo  : {}", pseudo);
                return 0;
            }
            LOG_THIS_WARN("❌ Unknown identity subcommand: {}", subcmd);
            return 1;
        }

    private:
        int cmd_create_identity(
            domain::ProjectConfig cfg,
            const std::vector<std::string> &args
        ) {
            if (args.size() < 5) {
                LOG_THIS_INFO("Usage: utx identity create <wallet_path> <pseudo> [--target <api>]");
                return 1;
            }

            const std::filesystem::path wallet_out = args[3];
            const std::string pseudo = args[4];

            std::string target = "127.0.0.1:8080";
            if (!cfg.api_target.empty()) {
                target = cfg.api_target;
            }

            for (size_t i = 4; i < args.size(); ++i) {
                if (args[i] == "--target" && i + 1 < args.size()) {
                    target = args[++i];
                }
            }

            infrastructure::chain::NetworkClient net{target};

            const bool wallet_already_exists =
                std::filesystem::exists(wallet_out);

            auto kp = infra::wallet::WalletHelper::generate_keypair();

            if (wallet_already_exists) {
                auto r =
                    infrastructure::deploy::DeployConfigManager::load_wallet_from_config(
                        domain::ProjectConfig{
                            .wallet_path = wallet_out.string()
                        }
                    );

                if (!r) {
                    LOG_THIS_ERROR(
                        "❌ Wallet exists but cannot be loaded: {}",
                        r.error()
                    );
                    return 1;
                }

                kp = *r;
            } else {
                auto saved = save_wallet_atomic(wallet_out, kp);
                if (!saved) {
                    LOG_THIS_ERROR(
                        "❌ Failed to persist wallet before deploy: {}",
                        saved.error()
                    );
                    return 1;
                }

                LOG_THIS_INFO(
                    "🔐 Wallet saved to {} before network admission.",
                    wallet_out.string()
                );
            }

            cfg.wallet_path =
                std::filesystem::absolute(wallet_out).string();
            cfg.api_target = target;
            infrastructure::deploy::save_local_config(ctx_.root, cfg);

            utx::domain::model::Address my_addr(kp.address);

            LOG_THIS_INFO("🧠 Checking existing identity graph...");

            auto graph_opt = net.get_graph(my_addr);

            if (graph_opt) {
                LOG_THIS_INFO("ℹ️ Identity already exists on network.");
            } else {
                LOG_THIS_INFO("🛠️ Identity will be created.");
                graph_opt = std::optional<utx::domain::graph::Graph>(
                    common::UUID(my_addr.to_string())
                );
            }

            graph_opt->root()->set_property("user.pseudo", pseudo);
            nlohmann::json identity_content = graph_opt->to_json();

            infrastructure::deploy::DeployClient deploy_client{target};

            domain::DeployRequest req;
            req.chain_id = my_addr.to_string();
            req.file_path = "identity";
            req.kind = "identity";
            req.projector = "IdentityProjector";
            req.content = identity_content.dump();
            req.commit_message = "Create identity";
            req.force_snapshot = true;

            auto deploy_res = deploy_client.deploy(req, kp);
            if (!deploy_res) {
                LOG_THIS_ERROR(
                    "❌ Identity deploy failed: {}",
                    deploy_res.error()
                );
                LOG_THIS_WARN(
                    "ℹ️ Wallet remains stored at {} so the operation can be retried safely.",
                    wallet_out.string()
                );
                return 1;
            }

            LOG_THIS_INFO(
                "✅ Identity finalized and wallet saved to {}!",
                wallet_out.string()
            );

            return 0;
        }

        static std::expected<void, std::string> save_wallet_atomic(
            const std::filesystem::path &wallet_path,
            const infra::wallet::KeyPair &kp
        ) {
            try {
                const auto parent = wallet_path.parent_path();
                if (!parent.empty()) {
                    std::filesystem::create_directories(parent);
                }

                const auto tmp =
                    std::filesystem::path(wallet_path.string() + ".tmp");

                {
                    std::ofstream out(tmp, std::ios::trunc);
                    if (!out) {
                        return std::unexpected(
                            "cannot open temporary wallet file " +
                            tmp.string()
                        );
                    }

                    out << json(kp).dump(4);
                    out.flush();

                    if (!out) {
                        return std::unexpected(
                            "cannot write temporary wallet file " +
                            tmp.string()
                        );
                    }
                }

                std::error_code ec;
                std::filesystem::rename(tmp, wallet_path, ec);
                if (ec) {
                    std::error_code remove_ec;
                    std::filesystem::remove(wallet_path, remove_ec);
                    ec.clear();
                    std::filesystem::rename(tmp, wallet_path, ec);
                }

                if (ec) {
                    std::error_code cleanup_ec;
                    std::filesystem::remove(tmp, cleanup_ec);
                    return std::unexpected(
                        "cannot replace wallet file: " + ec.message()
                    );
                }

                return {};
            } catch (const std::exception &e) {
                return std::unexpected(e.what());
            }
        }

        static std::string identity_config_to_b64(const infra::wallet::KeyPair &kp, const std::string &api_target) {
            const nlohmann::json j = {
                {"v", 1},
                {"curve", "secp256k1"},
                {"priv_hex", kp.private_key_hex},
                {"pub_hex", kp.public_key_hex},
                {"address", kp.address},
                {"api_target", api_target}
            };
            return common::base64::encode(j.dump());
        }

        infrastructure::context::AppContext ctx_;
    };
}
