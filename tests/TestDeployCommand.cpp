#include <atomic>
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
#include "infrastructure/deploy_config/StagingManager.hpp"
#include "use_case/AddCommand.hpp"
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
    {
        std::ofstream out(project.root / "app.js");
        out << "console.log('unstaged change');";
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
    ctx.deploy_config.targets.push_back(DeployTarget{
        .path = "app.js",
        .chain = "chain-unstaged",
        .kind = TargetKind::Js,
        .last_synced_hash = "previous-js-hash"
    });

    ASSERT_TRUE(
        utx::app::infrastructure::deploy::DeployConfigManager::save_deploy_config_atomic(
            ctx.root,
            ctx.deploy_config
        ).has_value()
    );

    ASSERT_TRUE(utx::app::infrastructure::deploy::StagingManager::save(
        project.root, {"index.html"}
    ).has_value());
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
    ASSERT_EQ(saved->targets.size(), 2U);
    EXPECT_EQ(saved->targets[1].last_synced_hash, "previous-js-hash");
    EXPECT_EQ(
        saved->targets.front().last_synced_hash,
        utx::common::md5_hex(content)
    );
    const auto staging = utx::app::infrastructure::deploy::StagingManager::load(project.root);
    ASSERT_TRUE(staging.has_value());
    EXPECT_TRUE(staging->empty());
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

    // A clean but explicitly staged target asks deploy to synchronize the
    // project manifest without unnecessarily redeploying that target.
    const std::string clean_content = "already finalized";
    {
        std::ofstream out(project.root / "clean.js");
        out << clean_content;
    }
    ctx.deploy_config.targets.push_back(DeployTarget{
        .path = "clean.js",
        .chain = "clean-chain",
        .kind = TargetKind::Js,
        .last_synced_hash = utx::common::md5_hex(clean_content)
    });

    ASSERT_TRUE(
        utx::app::infrastructure::deploy::DeployConfigManager::save_deploy_config_atomic(
            ctx.root,
            ctx.deploy_config
        ).has_value()
    );
    ASSERT_TRUE(
        utx::app::infrastructure::deploy::StagingManager::save(
            project.root, {"clean.js"}
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

    const auto staged =
        utx::app::infrastructure::deploy::StagingManager::load(project.root);
    ASSERT_TRUE(staged.has_value());
    EXPECT_TRUE(staged->empty());
}


TEST(DeployCommandTest, PartialFailureRetainsOnlyFailedSelectionForRetry) {
    LocalDeployServer http;
    std::atomic<bool> reject_b{true};
    std::atomic<int> a_prepares{0};
    std::atomic<int> b_prepares{0};
    std::atomic<int> c_prepares{0};

    http.server.Post(
        "/api/deploy/prepare",
        [&](const httplib::Request& req, httplib::Response& res) {
            const auto body = nlohmann::json::parse(req.body);
            const auto chain = body.at("chain_id").get<std::string>();
            if (chain == "chain-a") ++a_prepares;
            if (chain == "chain-b") ++b_prepares;
            if (chain == "chain-c") ++c_prepares;
            if (chain == "chain-b" && reject_b.load()) {
                res.status = 400;
                res.set_content(R"({"error":"temporary rejection"})", "application/json");
                return;
            }
            res.set_content(nlohmann::json{
                {"plan_id", "plan-" + chain},
                {"ring_reference", nlohmann::json{{"version", "ring-v1"}}},
                {"transactions", nlohmann::json::array({
                    {{"payload_data", "urn:pi:test:content"}}
                })}
            }.dump(), "application/json");
        }
    );

    http.server.Post(
        "/api/deploy/submit",
        [](const httplib::Request&, httplib::Response& res) {
            res.status = 202;
            res.set_content(nlohmann::json{
                {"status", "queued"},
                {"pending_block_ids", nlohmann::json::array({"pending-1"})}
            }.dump(), "application/json");
        }
    );

    http.server.Get(
        R"(/chain/([^/]+)/pending/([^/]+))",
        [](const httplib::Request& req, httplib::Response& res) {
            res.set_content(nlohmann::json{
                {"id", req.matches[2].str()},
                {"chain_address", req.matches[1].str()},
                {"state", "Finalized"},
                {"error", nullptr}
            }.dump(), "application/json");
        }
    );
    http.start();

    TempProject project;
    for (const auto& name : {"a.html", "b.html", "c.html"}) {
        std::ofstream out(project.root / name);
        out << "changed " << name;
    }

    AppContext ctx;
    ctx.root = project.root;
    ctx.project_config.api_target = http.base_url();
    ctx.network_client.target = http.base_url();
    ctx.wallet = WalletHelper::generate_keypair();
    for (const auto& name : {"a", "b", "c"}) {
        ctx.deploy_config.targets.push_back(DeployTarget{
            .path = std::string(name) + ".html",
            .chain = std::string("chain-") + name,
            .kind = TargetKind::Html,
            .last_synced_hash = "old-content"
        });
    }

    ASSERT_TRUE(utx::app::infrastructure::deploy::DeployConfigManager::
        save_deploy_config_atomic(ctx.root, ctx.deploy_config).has_value());
    ASSERT_TRUE(utx::app::infrastructure::deploy::StagingManager::
        save(ctx.root, {"a.html", "b.html"}).has_value());

    EXPECT_EQ(DeployCommand(ctx).execute({"utx", "deploy", "first try"}), 1);

    utx::app::infrastructure::deploy::DeployConfigManager manager;
    const auto after_first = manager.load_deploy_config(project.root);
    ASSERT_TRUE(after_first.has_value());
    ASSERT_EQ(after_first->targets.size(), 3U);
    EXPECT_NE(after_first->targets[0].last_synced_hash, "old-content");
    EXPECT_EQ(after_first->targets[1].last_synced_hash, "old-content");
    EXPECT_EQ(after_first->targets[2].last_synced_hash, "old-content");

    const auto staged_after_first =
        utx::app::infrastructure::deploy::StagingManager::load(project.root);
    ASSERT_TRUE(staged_after_first.has_value());
    EXPECT_EQ(*staged_after_first, (std::vector<std::string>{"b.html"}));
    EXPECT_EQ(a_prepares.load(), 1);
    EXPECT_EQ(b_prepares.load(), 1);
    EXPECT_EQ(c_prepares.load(), 0);

    reject_b = false;
    ctx.deploy_config = *after_first;
    EXPECT_EQ(DeployCommand(ctx).execute({"utx", "deploy", "retry"}), 0);

    const auto after_retry = manager.load_deploy_config(project.root);
    ASSERT_TRUE(after_retry.has_value());
    EXPECT_NE(after_retry->targets[1].last_synced_hash, "old-content");
    EXPECT_EQ(after_retry->targets[2].last_synced_hash, "old-content");
    const auto staged_after_retry =
        utx::app::infrastructure::deploy::StagingManager::load(project.root);
    ASSERT_TRUE(staged_after_retry.has_value());
    EXPECT_TRUE(staged_after_retry->empty());
    EXPECT_EQ(a_prepares.load(), 1);
    EXPECT_EQ(b_prepares.load(), 2);
    EXPECT_EQ(c_prepares.load(), 0);
}
