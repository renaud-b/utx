#pragma once

#include <filesystem>
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

            std::string wallet_out = args[3];
            std::string pseudo = args[4];

            std::string target = "127.0.0.1:8080";
            if (!cfg.api_target.empty()) {
                target = cfg.api_target;
            }

            for (size_t i = 4; i < args.size(); ++i) {
                if (args[i] == "--target" && i + 1 < args.size())
                    target = args[++i];
            }

            infrastructure::chain::NetworkClient net{target};

            // 🔑 Wallet
            auto kp = infra::wallet::WalletHelper::generate_keypair();

            if (std::filesystem::exists(wallet_out)) {
                auto r = infrastructure::deploy::DeployConfigManager::load_wallet_from_config(
                    domain::ProjectConfig{.wallet_path = wallet_out});

                if (!r) {
                    LOG_THIS_ERROR("❌ Wallet exists but cannot be loaded: {}", r.error());
                    return 1;
                }

                kp = *r;
            }

            utx::domain::model::Address my_addr(kp.address);

            LOG_THIS_INFO("🧠 Checking existing identity graph...");

            // 🔥 Fetch graph (nouvelle logique)
            auto graph_opt = net.get_graph(my_addr);

            if (graph_opt) {
                LOG_THIS_INFO("ℹ️ Identity already exists on network.");
            } else {
                LOG_THIS_INFO("🛠️ Identity will be created.");
                graph_opt = std::optional<utx::domain::graph::Graph>(common::UUID(my_addr.to_string()));
            }

            // 🔥 Build minimal "content"
            graph_opt->root()->set_property("user.pseudo", pseudo);
            nlohmann::json identity_content = graph_opt->to_json();

            auto deploy_client = ctx_.deploy_client();

            domain::DeployRequest req;
            req.chain_id = my_addr.to_string();
            req.file_path = "identity";
            req.projector = "IdentityProjector"; // ou to_string(TargetKind::Identity)
            req.content = identity_content.dump();
            req.commit_message = "Create identity";
            req.force_snapshot = true;

            // 🔥 PREPARE (node = brain)
            auto plan_res = deploy_client.prepare(req, my_addr.to_string());

            if (!plan_res) {
                LOG_THIS_ERROR("❌ Deploy prepare failed: {}", plan_res.error());
                return 1;
            }

            const auto &plan = *plan_res;

            if (!plan.contains("transactions") || !plan["transactions"].is_array()) {
                LOG_THIS_ERROR("❌ Invalid plan returned by node");
                return 1;
            }

            const auto &txs = plan["transactions"];

            if (txs.empty()) {
                LOG_THIS_WARN("⚠️ Nothing to deploy.");
                return 0;
            }

            // 🔥 SIGN
            nlohmann::json signed_txs = nlohmann::json::array();

            for (const auto &tx: txs) {
                const std::string payload = tx["payload_data"].get<std::string>();

                auto signed_res =
                        deploy_client.build_signed_tx(payload, kp);

                if (!signed_res) {
                    LOG_THIS_ERROR("❌ Signing failed: {}", signed_res.error());
                    return 1;
                }

                signed_txs.push_back(signed_res.value());
            }

            // 🔥 SUBMIT
            auto submit_res =
                    deploy_client.submit(plan["plan_id"], my_addr.to_string(), signed_txs);

            if (!submit_res) {
                LOG_THIS_ERROR("❌ Submit failed: {}", submit_res.error());
                return 1;
            }

            // 💾 Save wallet
            std::ofstream f(wallet_out);
            f << json(kp).dump(4);

            cfg.wallet_path = std::filesystem::absolute(wallet_out).string();
            cfg.api_target = target;
            ctx_.save_local_config();

            LOG_THIS_INFO("✅ Identity deployed and saved to {}!", wallet_out);

            return 0;
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
