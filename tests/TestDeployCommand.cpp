#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "common/Hash.hpp"
#include "infrastructure/context/AppContext.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"
#include "infrastructure/wallet/WalletHelper.hpp"
#include "use_case/DeployCommand.hpp"

namespace {

using utx::app::domain::DeployTarget;
using utx::app::domain::TargetKind;
using utx::app::infrastructure::context::AppContext;
using utx::app::use_case::DeployCommand;
using utx::infra::wallet::WalletHelper;

class LocalDeployServer {
public:
    ~LocalDeployServer() {
        server.stop();
        if (thread.joinable()) {
            thread.join();
        }
    }

    void start() {
        port = server.bind_to_any_port("127.0.0.1");
        ASSERT_GT(port, 0);
        thread = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }

    [[nodiscard]]
    std::string base_url() const {
        return "http://127.0.0.1:" + std::to_string(port);
    }

    httplib::Server server;

private:
    int port{-1};
    std::thread thread;
};

class TempProject {
public:
    TempProject() {
        root = std::filesystem::temp_directory_path() /
            ("utx-deploy-command-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ));
        std::filesystem::create_directories(root);
    }

    ~TempProject() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::filesystem::path root;
};

TEST(DeployCommandTest, DeploysModifiedTargetAndMarksItSyncedAfterFinalization) {
    LocalDeployServer http;
    nlohmann::json captured_prepare;

    http.server.Post(
        "/api/deploy/prepare",
        [&](const httplib::Request& req, httplib::Response& res) {
            captured_prepare = nlohmann::json::parse(req.body);
            res.set_content(
                nlohmann::json{
                    {"plan_id", "plan-1"},
                    {"ring_reference", nlohmann::json{{"version", "ring-v1"}}},
                    {"transactions", nlohmann::json::array({
                        {{"payload_data", "urn:pi:test:payload"}}
                    })}
                }.dump(),
                "application/json"
            );
        }
    );

    http.server.Post(
        "/api/deploy/submit",
        [](const httplib::Request&, httplib::Response& res) {
            res.status = 202;
            res.set_content(
                nlohmann::json{
                    {"status", "queued"},
                    {"pending_block_ids", nlohmann::json::array({"pending-1"})}
                }.dump(),
                "application/json"
            );
        }
    );

    http.server.Get(
        R"(/chain/([^/]+)/pending/([^/]+))",
        [](const httplib::Request& req, httplib::Response& res) {
            res.set_content(
                nlohmann::json{
                    {"id", req.matches[2].str()},
                    {"chain_address", req.matches[1].str()},
                    {"state", "Finalized"},
                    {"error", nullptr}
                }.dump(),
                "application/json"
            );
        }
    );

    http.start();

    TempProject project;
    const std::string content = "<html><body>Hello</body></html>";

    {
        std::ofstream out(project.root / "index.html");
        out << content;
    }

    AppContext ctx;
    ctx.root = project.root;
    ctx.project_config.api_target = http.base_url();
    ctx.wallet = WalletHelper::generate_keypair();
    ctx.network_client.target = http.base_url();
    ctx.deploy_config.targets.push_back(DeployTarget{
        .path = "index.html",
        .chain = "chain-1",
        .kind = TargetKind::Html,
        .last_synced_hash = {}
    });

    ASSERT_TRUE(
        utx::app::infrastructure::deploy::DeployConfigManager::save_deploy_config_atomic(
            ctx.root,
            ctx.deploy_config
        ).has_value()
    );

    const int rc = DeployCommand(std::move(ctx)).execute(
        {"utx", "deploy", "ship it", "--force-snapshot"}
    );

    ASSERT_EQ(rc, 0);
    ASSERT_FALSE(captured_prepare.is_null());
    EXPECT_EQ(captured_prepare.at("chain_id"), "chain-1");
    EXPECT_EQ(captured_prepare.at("content"), content);
    EXPECT_EQ(captured_prepare.at("commit_message"), "ship it");
    EXPECT_TRUE(captured_prepare.at("force_snapshot").get<bool>());

    utx::app::infrastructure::deploy::DeployConfigManager manager;
    const auto saved = manager.load_deploy_config(project.root);
    ASSERT_TRUE(saved.has_value()) << saved.error();
    ASSERT_EQ(saved->targets.size(), 1U);
    EXPECT_EQ(
        saved->targets.front().last_synced_hash,
        utx::common::md5_hex(content)
    );
}

} // namespace


TEST(DeployCommandTest, PublishesProjectManifestThroughJsonDeployPlanner) {
    LocalDeployServer http;
    nlohmann::json captured_prepare;
    std::atomic<int> submit_calls{0};

    http.server.Post(
        "/api/deploy/prepare",
        [&](const httplib::Request& req, httplib::Response& res) {
            captured_prepare = nlohmann::json::parse(req.body);
            res.set_content(
                nlohmann::json{
                    {"plan_id", "manifest-plan"},
                    {"transactions", nlohmann::json::array()}
                }.dump(),
                "application/json"
            );
        }
    );

    http.server.Post(
        "/api/deploy/submit",
        [&](const httplib::Request&, httplib::Response& res) {
            ++submit_calls;
            res.status = 500;
        }
    );

    http.start();

    TempProject project;
    AppContext ctx;
    ctx.root = project.root;
    ctx.project_config.api_target = http.base_url();
    ctx.project_config.deploy_chain = "manifest-chain";
    ctx.wallet = WalletHelper::generate_keypair();
    ctx.network_client.target = http.base_url();

    ASSERT_TRUE(
        utx::app::infrastructure::deploy::DeployConfigManager::save_deploy_config_atomic(
            ctx.root,
            ctx.deploy_config
        ).has_value()
    );

    const auto manifest_content =
        utx::common::io::read_file(
            (project.root / utx::app::infrastructure::deploy::kDeployFile).string()
        );

    const int rc = DeployCommand(std::move(ctx)).execute(
        {"utx", "deploy", "sync manifest"}
    );

    ASSERT_EQ(rc, 0);
    ASSERT_FALSE(captured_prepare.is_null());
    EXPECT_EQ(captured_prepare.at("chain_id"), "manifest-chain");
    EXPECT_EQ(captured_prepare.at("file_path"), ".utx.deploy.json");
    EXPECT_EQ(captured_prepare.at("kind"), "json");
    EXPECT_EQ(captured_prepare.at("content"), manifest_content);
    EXPECT_EQ(captured_prepare.at("commit_message"), "sync manifest [manifest]");
    EXPECT_FALSE(captured_prepare.at("force_snapshot").get<bool>());
    EXPECT_EQ(submit_calls.load(), 0);
}
