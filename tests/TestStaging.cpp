#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "common/Hash.hpp"
#include "infrastructure/context/AppContext.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"
#include "infrastructure/deploy_config/StagingManager.hpp"
#include "use_case/AddCommand.hpp"
#include "use_case/DeployCommand.hpp"

namespace {
using utx::app::domain::DeployTarget;
using utx::app::domain::TargetKind;
using utx::app::infrastructure::context::AppContext;
using utx::app::infrastructure::deploy::StagingManager;

struct Project {
    std::filesystem::path root{
        std::filesystem::temp_directory_path() /
        ("utx-staging-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))
    };
    Project() { std::filesystem::create_directories(root); }
    ~Project() { std::error_code ec; std::filesystem::remove_all(root, ec); }
    void write(const std::string& path, const std::string& value) const {
        std::ofstream out(root / path); out << value;
    }
    void save(const utx::app::domain::DeployConfig& cfg) const {
        ASSERT_TRUE(utx::app::infrastructure::deploy::DeployConfigManager::
            save_deploy_config_atomic(root, cfg).has_value());
    }
};

TEST(StagingTest, AddExistingTargetStagesWithoutClearingSyncedHash) {
    Project p;
    p.write("index.html", "<p>before</p>");
    AppContext ctx;
    ctx.root = p.root;
    ctx.deploy_config.targets.push_back(DeployTarget{
        .path = "index.html", .chain = "original-chain",
        .kind = TargetKind::Html, .last_synced_hash = "existing-hash"
    });
    p.save(ctx.deploy_config);
    const auto rc = utx::app::use_case::AddCommand(ctx).execute(
        {"utx", "add", (p.root / "index.html").string()});
    ASSERT_EQ(rc, 0);
    utx::app::infrastructure::deploy::DeployConfigManager manager;
    const auto config = manager.load_deploy_config(p.root);
    ASSERT_TRUE(config.has_value());
    ASSERT_EQ(config->targets.size(), 1U);
    EXPECT_EQ(config->targets[0].chain, "original-chain");
    EXPECT_EQ(config->targets[0].last_synced_hash, "existing-hash");
    const auto staged = StagingManager::load(p.root);
    ASSERT_TRUE(staged.has_value());
    EXPECT_EQ(*staged, std::vector<std::string>{"index.html"});
}

TEST(StagingTest, NoStagingDoesNotDeployModifiedTrackedFiles) {
    Project p;
    p.write("index.html", "changed");
    AppContext ctx;
    ctx.root = p.root;
    ctx.wallet = utx::infra::wallet::WalletHelper::generate_keypair();
    ctx.deploy_config.targets.push_back(DeployTarget{
        .path = "index.html", .chain = "chain-a",
        .kind = TargetKind::Html, .last_synced_hash = "old"
    });
    p.save(ctx.deploy_config);
    EXPECT_EQ(utx::app::use_case::DeployCommand(ctx).execute(
        {"utx", "deploy", "only staged"}), 0);
    utx::app::infrastructure::deploy::DeployConfigManager manager;
    const auto config = manager.load_deploy_config(p.root);
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->targets[0].last_synced_hash, "old");
}

TEST(StagingTest, MalformedSelectionFailsClosed) {
    Project p;
    std::filesystem::create_directories(p.root / ".utx");
    p.write(".utx/staging.json", "not valid json");
    EXPECT_FALSE(StagingManager::load(p.root).has_value());
}

TEST(StagingTest, MissingSelectedFileFailsWithoutDroppingSelection) {
    Project p;
    AppContext ctx;
    ctx.root = p.root;
    ctx.wallet = utx::infra::wallet::WalletHelper::generate_keypair();
    ctx.deploy_config.targets.push_back(DeployTarget{
        .path = "missing.html", .chain = "chain-a", .kind = TargetKind::Html
    });
    p.save(ctx.deploy_config);
    ASSERT_TRUE(StagingManager::save(p.root, {"missing.html"}).has_value());
    EXPECT_EQ(utx::app::use_case::DeployCommand(ctx).execute(
        {"utx", "deploy", "missing"}), 1);
    const auto staged = StagingManager::load(p.root);
    ASSERT_TRUE(staged.has_value());
    EXPECT_EQ(*staged, std::vector<std::string>{"missing.html"});
}
} // namespace
