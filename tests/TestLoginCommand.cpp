#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "common/Base64.hpp"
#include "infrastructure/context/AppContext.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"
#include "infrastructure/wallet/WalletHelper.hpp"
#include "use_case/LoginCommand.hpp"

namespace {

using utx::app::infrastructure::context::AppContext;
using utx::app::infrastructure::deploy::DeployConfigManager;
using utx::app::use_case::LoginCommand;
using utx::infra::wallet::WalletHelper;

class LocalLoginServer {
public:
    ~LocalLoginServer() {
        server.stop();
        if (thread.joinable()) {
            thread.join();
        }
    }

    void start() {
        server.Get(
            R"(/graph/(.+))",
            [](const httplib::Request&, httplib::Response& res) {
                res.status = 404;
            }
        );

        port = server.bind_to_any_port("127.0.0.1");
        ASSERT_GT(port, 0);
        thread =
            std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }

    [[nodiscard]]
    std::string base_url() const {
        return "http://127.0.0.1:" + std::to_string(port);
    }

private:
    httplib::Server server;
    std::thread thread;
    int port{-1};
};

class TempLoginProject {
public:
    TempLoginProject() {
        root =
            std::filesystem::temp_directory_path() /
            (
                "utx-login-command-" +
                std::to_string(
                    std::chrono::steady_clock::now()
                        .time_since_epoch()
                        .count()
                )
            );
        std::filesystem::create_directories(root);
    }

    ~TempLoginProject() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::filesystem::path root;
};

std::string identity_b64(
    const utx::infra::wallet::KeyPair& wallet,
    const std::string& api_target
) {
    return utx::common::base64::encode(
        nlohmann::json{
            {"v", 1},
            {"curve", "secp256k1"},
            {"priv_hex", wallet.private_key_hex},
            {"pub_hex", wallet.public_key_hex},
            {"address", wallet.address},
            {"api_target", api_target}
        }.dump()
    );
}

TEST(LoginCommandTest, ImportsIdentityConfigAndPersistsLocalWallet) {
    LocalLoginServer http;
    http.start();

    TempLoginProject project;
    const auto wallet = WalletHelper::generate_keypair();

    AppContext ctx;
    ctx.root = project.root;

    const auto encoded =
        identity_b64(wallet, http.base_url());

    ASSERT_EQ(
        LoginCommand(ctx).execute(
            {"utx", "login", "--b64", encoded}
        ),
        0
    );

    const auto wallet_path =
        project.root / ".utx" / "wallet.json";
    ASSERT_TRUE(std::filesystem::exists(wallet_path));

    const auto loaded =
        WalletHelper::load_from_file(wallet_path.string());
    EXPECT_EQ(loaded.private_key_hex, wallet.private_key_hex);
    EXPECT_EQ(loaded.public_key_hex, wallet.public_key_hex);
    EXPECT_EQ(loaded.address, wallet.address);

    DeployConfigManager manager;
    const auto config =
        manager.load_local_config(project.root);
    EXPECT_EQ(
        config.wallet_path,
        std::filesystem::absolute(wallet_path).string()
    );
    EXPECT_EQ(config.api_target, http.base_url());
}

TEST(LoginCommandTest, ExplicitTargetOverridesIdentityConfigTarget) {
    LocalLoginServer http;
    http.start();

    TempLoginProject project;
    const auto wallet = WalletHelper::generate_keypair();

    AppContext ctx;
    ctx.root = project.root;

    ASSERT_EQ(
        LoginCommand(ctx).execute({
            "utx",
            "login",
            "--b64",
            identity_b64(wallet, "http://invalid.example"),
            "--target",
            http.base_url()
        }),
        0
    );

    DeployConfigManager manager;
    const auto config =
        manager.load_local_config(project.root);
    EXPECT_EQ(config.api_target, http.base_url());
}

TEST(LoginCommandTest, RejectsIdentityAddressThatDoesNotMatchPublicKey) {
    TempLoginProject project;
    const auto wallet = WalletHelper::generate_keypair();

    auto invalid = nlohmann::json{
        {"v", 1},
        {"curve", "secp256k1"},
        {"priv_hex", wallet.private_key_hex},
        {"pub_hex", wallet.public_key_hex},
        {"address", "0x0000000000000000000000000000000000000000"},
        {"api_target", "127.0.0.1:8080"}
    };

    AppContext ctx;
    ctx.root = project.root;

    ASSERT_EQ(
        LoginCommand(ctx).execute({
            "utx",
            "login",
            "--b64",
            utx::common::base64::encode(invalid.dump())
        }),
        1
    );

    EXPECT_FALSE(
        std::filesystem::exists(
            project.root / ".utx" / "wallet.json"
        )
    );
}

} // namespace
