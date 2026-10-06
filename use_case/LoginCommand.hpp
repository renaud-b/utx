#pragma once

#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "AbstractCommand.hpp"
#include "common/Base64.hpp"
#include "common/Logger.hpp"

#include "domain/Types.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"
#include "infrastructure/chain/NetworkClient.hpp"

namespace utx::app::use_case {
    /** cmd_login : Log in to a wallet and set it as the active session.
     *
     * utx login <wallet_path> [--target <api>]
     * utx login --b64 <identity_config> [--target <api>]
     */
    class LoginCommand final : public AbstractCommand {
    public:
        explicit LoginCommand(
            const infrastructure::context::AppContext& ctx
        ) : ctx_(ctx) {}

        [[nodiscard]]
        int execute(const std::vector<std::string>& args) override {
            if (args.size() < 3) {
                print_usage();
                return 1;
            }

            const bool import_b64 = args[2] == "--b64";
            if (import_b64 && args.size() < 4) {
                print_usage();
                return 1;
            }

            const std::size_t options_begin = import_b64 ? 4 : 3;
            std::optional<std::string> target_override;
            for (std::size_t i = options_begin; i < args.size(); ++i) {
                if (args[i] == "--target" && i + 1 < args.size()) {
                    target_override = args[++i];
                }
            }

            infra::wallet::KeyPair wallet;
            std::filesystem::path wallet_path;
            std::string target = "127.0.0.1:8080";

            if (import_b64) {
                const auto imported = decode_identity_config(args[3]);
                if (!imported) {
                    LOG_THIS_ERROR(
                        "❌ Failed to import identity config: {}",
                        imported.error()
                    );
                    return 1;
                }

                wallet = imported->wallet;
                target = imported->api_target.empty()
                    ? target
                    : imported->api_target;

                wallet_path =
                    ctx_.root /
                    infrastructure::deploy::kUtxDir /
                    "wallet.json";

                const auto saved =
                    save_wallet_atomic(wallet_path, wallet);
                if (!saved) {
                    LOG_THIS_ERROR(
                        "❌ Failed to persist imported wallet: {}",
                        saved.error()
                    );
                    return 1;
                }
            } else {
                wallet_path = args[2];

                if (!std::filesystem::exists(wallet_path)) {
                    LOG_THIS_ERROR(
                        "❌ Wallet file '{}' not found.",
                        wallet_path.string()
                    );
                    return 1;
                }

                try {
                    wallet =
                        infra::wallet::WalletHelper::load_from_file(
                            wallet_path.string()
                        );
                } catch (const std::exception& e) {
                    LOG_THIS_ERROR(
                        "❌ Failed to load wallet from '{}': {}",
                        wallet_path.string(),
                        e.what()
                    );
                    return 1;
                }
            }

            if (target_override) {
                target = *target_override;
            }

            const infrastructure::chain::NetworkClient net{target};
            if (const auto graph =
                    net.fetch_graph_state(wallet.address);
                !graph) {
                LOG_THIS_WARN(
                    "⚠️ Wallet loaded but no identity found on chain. (Did you use create-identity?)"
                );
            } else {
                LOG_THIS_INFO(
                    "🌐 Welcome back, {}!",
                    graph->root()->get_property("user.pseudo")
                );
            }

            auto cfg = ctx_.project_config;
            cfg.wallet_path =
                std::filesystem::absolute(wallet_path).string();
            cfg.api_target = target;
            infrastructure::deploy::save_local_config(
                ctx_.root,
                cfg
            );

            LOG_THIS_INFO(
                "{}✅ Session active for {}{}",
                utx::app::domain::color::green,
                wallet.address,
                utx::app::domain::color::reset
            );
            return 0;
        }

    private:
        struct ImportedIdentity {
            infra::wallet::KeyPair wallet;
            std::string api_target;
        };

        static void print_usage() {
            LOG_THIS_INFO(
                "Usage: utx login <wallet_path> [--target <api>]"
            );
            LOG_THIS_INFO(
                "       utx login --b64 <identity_config> [--target <api>]"
            );
            LOG_THIS_INFO(
                "--target <api> : Override the API target stored in the identity config."
            );
        }

        static std::expected<ImportedIdentity, std::string>
        decode_identity_config(const std::string& encoded) {
            try {
                const auto decoded =
                    common::base64::decode(encoded);
                const auto j =
                    nlohmann::json::parse(decoded);

                if (j.value("v", 0) != 1) {
                    return std::unexpected(
                        "unsupported identity config version"
                    );
                }

                if (j.value("curve", std::string{}) !=
                    "secp256k1") {
                    return std::unexpected(
                        "unsupported identity curve"
                    );
                }

                const infra::wallet::KeyPair wallet{
                    .private_key_hex =
                        j.at("priv_hex").get<std::string>(),
                    .public_key_hex =
                        j.at("pub_hex").get<std::string>(),
                    .address =
                        j.at("address").get<std::string>()
                };

                const auto derived_address =
                    infra::wallet::WalletHelper::derive_address(
                        wallet.public_key_hex
                    );
                if (derived_address != wallet.address) {
                    return std::unexpected(
                        "identity address does not match public key"
                    );
                }

                return ImportedIdentity{
                    .wallet = wallet,
                    .api_target =
                        j.value("api_target", std::string{})
                };
            } catch (const std::exception& e) {
                return std::unexpected(e.what());
            }
        }

        static std::expected<void, std::string>
        save_wallet_atomic(
            const std::filesystem::path& wallet_path,
            const infra::wallet::KeyPair& wallet
        ) {
            try {
                std::filesystem::create_directories(
                    wallet_path.parent_path()
                );

                const auto tmp = std::filesystem::path(
                    wallet_path.string() + ".tmp"
                );

                {
                    std::ofstream out(tmp, std::ios::trunc);
                    if (!out) {
                        return std::unexpected(
                            "cannot open temporary wallet file " +
                            tmp.string()
                        );
                    }

                    out << nlohmann::json(wallet).dump(4);
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
                    std::filesystem::remove(
                        wallet_path,
                        remove_ec
                    );
                    ec.clear();
                    std::filesystem::rename(
                        tmp,
                        wallet_path,
                        ec
                    );
                }

                if (ec) {
                    std::error_code cleanup_ec;
                    std::filesystem::remove(tmp, cleanup_ec);
                    return std::unexpected(
                        "cannot replace wallet file: " +
                        ec.message()
                    );
                }

                return {};
            } catch (const std::exception& e) {
                return std::unexpected(e.what());
            }
        }

        infrastructure::context::AppContext ctx_;
    };
}
