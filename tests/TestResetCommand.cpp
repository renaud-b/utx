#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "domain/Types.hpp"
#include "infrastructure/context/AppContext.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"
#include "use_case/ResetCommand.hpp"

namespace {

using utx::app::domain::DeployTarget;
using utx::app::domain::TargetKind;
using utx::app::infrastructure::context::AppContext;
using utx::app::infrastructure::deploy::DeployConfigManager;
using utx::app::use_case::ResetCommand;

class TempResetProject {
public:
    TempResetProject() {
        root = std::filesystem::temp_directory_path() /
            ("utx-reset-command-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ));
        std::filesystem::create_directories(root);
    }

    ~TempResetProject() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::filesystem::path root;
};

TEST(ResetCommandTest, ClearsSynchronizationHashesAndLegacyLabelsWhilePreservingProjectTopology) {
    TempResetProject project;

    const std::string content = "<html><body>Hello</body></html>";
    {
        std::ofstream out(project.root / "index.html");
        out << content;
    }

    AppContext ctx;
    ctx.root = project.root;
    ctx.project_config.api_target = "127.0.0.1:8080";
    ctx.project_config.wallet_path = "/tmp/wallet.json";
    ctx.project_config.deploy_chain = "manifest-chain";

    ctx.deploy_config.targets.push_back(DeployTarget{
        .path = "index.html",
        .chain = "content-chain",
        .kind = TargetKind::Html,
        .last_synced_hash = "previously-finalized-hash",
        .genesis_labels = {"site"}
    });
    ctx.deploy_config.targets.push_back(DeployTarget{
        .path = "app.js",
        .chain = "already-reset-chain",
        .kind = TargetKind::Js,
        .last_synced_hash = {},
        .genesis_labels = {}
    });

    ASSERT_TRUE(
        DeployConfigManager::save_deploy_config_atomic(
            ctx.root,
            ctx.deploy_config
        ).has_value()
    );

    utx::app::infrastructure::deploy::save_local_config(
        ctx.root,
        ctx.project_config
    );

    const int rc = ResetCommand(std::move(ctx)).execute(
        {"utx", "reset"}
    );

    ASSERT_EQ(rc, 0);

    DeployConfigManager manager;
    const auto saved = manager.load_deploy_config(project.root);
    ASSERT_TRUE(saved.has_value()) << saved.error();
    ASSERT_EQ(saved->targets.size(), 2U);

    const auto& target = saved->targets[0];
    EXPECT_EQ(target.path, "index.html");
    EXPECT_EQ(target.chain, "content-chain");
    EXPECT_EQ(target.kind, TargetKind::Html);
    EXPECT_TRUE(target.last_synced_hash.empty());
    EXPECT_TRUE(target.genesis_labels.empty());

    EXPECT_TRUE(saved->targets[1].last_synced_hash.empty());

    std::ifstream project_file(project.root / "index.html");
    const std::string preserved_content{
        std::istreambuf_iterator<char>{project_file},
        std::istreambuf_iterator<char>{}
    };
    EXPECT_EQ(preserved_content, content);

    const auto local_config = manager.load_local_config(project.root);
    EXPECT_EQ(local_config.api_target, "127.0.0.1:8080");
    EXPECT_EQ(local_config.wallet_path, "/tmp/wallet.json");
    EXPECT_EQ(local_config.deploy_chain, "manifest-chain");
}

TEST(ResetCommandTest, IsIdempotent) {
    TempResetProject project;

    AppContext ctx;
    ctx.root = project.root;
    ctx.deploy_config.targets.push_back(DeployTarget{
        .path = "index.html",
        .chain = "content-chain",
        .kind = TargetKind::Html,
        .last_synced_hash = {}
    });

    ASSERT_TRUE(
        DeployConfigManager::save_deploy_config_atomic(
            ctx.root,
            ctx.deploy_config
        ).has_value()
    );

    ASSERT_EQ(ResetCommand(ctx).execute({"utx", "reset"}), 0);
    ASSERT_EQ(ResetCommand(std::move(ctx)).execute({"utx", "reset"}), 0);

    DeployConfigManager manager;
    const auto saved = manager.load_deploy_config(project.root);
    ASSERT_TRUE(saved.has_value()) << saved.error();
    ASSERT_EQ(saved->targets.size(), 1U);
    EXPECT_TRUE(saved->targets.front().last_synced_hash.empty());
    EXPECT_EQ(saved->targets.front().chain, "content-chain");
}

} // namespace
