#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <gtest/gtest.h>
#include <httplib.h>
#include <thread>

#include "infrastructure/download/SafeDownload.hpp"
#include "infrastructure/download/ArtifactDownload.hpp"

namespace {
namespace fs = std::filesystem;
using namespace utx::app::infrastructure::download;
using utx::app::domain::DeployConfig;
using utx::app::domain::DeployTarget;

struct Temp : testing::Test {
    fs::path root = fs::temp_directory_path() / ("utx-safe-download-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp() override { fs::create_directories(root / "project"); }
    void TearDown() override { fs::remove_all(root); }
    fs::path project() const { return root / "project"; }
};

TEST_F(Temp, RejectsUnsafePathsBeforeAnyWrites) {
    for (const auto* path : {"../escape", "/tmp/out", "a/../../out",
            "a//b", "a/./b", "a/", "a\\b", ".utx/config.json",
            ".utx.deploy.json", ".utx.deploy.json.tmp", ".git/hooks/post-checkout", "a\nb"}) {
        DeployConfig config{.targets = {{.path = path, .chain = "chain"}}};
        EXPECT_FALSE(validate_destinations(project(), config)) << path;
        EXPECT_TRUE(fs::is_empty(project()));
    }
    EXPECT_FALSE(safe_chain_id("../chain"));
    EXPECT_FALSE(safe_chain_id("chain?other"));
    EXPECT_TRUE(safe_chain_id("AZ-ab_cd"));
}

TEST_F(Temp, RejectsDuplicateAndAncestorDestinations) {
    DeployConfig config{.targets = {{.path = "a", .chain = "one"},
                                    {.path = "a/b", .chain = "two"}}};
    EXPECT_FALSE(validate_destinations(project(), config));
    config.targets[1].path = "a";
    EXPECT_FALSE(validate_destinations(project(), config));
    EXPECT_TRUE(fs::is_empty(project()));
}

TEST_F(Temp, RejectsExistingFilesAndDoesNotFollowSymlinks) {
    std::ofstream(root / "outside") << "original";
    fs::create_symlink(root / "outside", project() / "file");
    fs::create_directory_symlink(root, project() / "link");
    DeployConfig config{.targets = {{.path = "file", .chain = "chain"}}};
    EXPECT_FALSE(validate_destinations(project(), config));
    EXPECT_FALSE(write_new_file(project(), "file", "modified"));
    config.targets[0].path = "link/escape";
    EXPECT_FALSE(validate_destinations(project(), config));
    EXPECT_FALSE(write_new_file(project(), "link/escape", "modified"));
    EXPECT_FALSE(fs::exists(root / "escape"));
    std::ifstream in(root / "outside");
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), {}), "original");
    fs::create_directory_symlink(root, project() / ".utx");
    EXPECT_FALSE(write_new_file(project(), ".utx/config.json", "bad"));
    EXPECT_FALSE(fs::exists(root / "config.json"));
}

TEST_F(Temp, WritesExactBytesAndNeverOverwrites) {
    const std::string bytes("a\0\xff\r\n", 5);
    DeployConfig config{.targets = {{.path = "tests/fixture.bin", .chain = "chain"}}};
    ASSERT_TRUE(validate_destinations(project(), config));
    ASSERT_TRUE(write_new_file(project(), "tests/fixture.bin", bytes));
    EXPECT_FALSE(write_new_file(project(), "tests/fixture.bin", "replacement"));
    EXPECT_FALSE(validate_destinations(project(), config));
    std::ifstream in(project() / "tests/fixture.bin", std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), {}), bytes);
}

TEST(ArtifactDownload, VerifiesBinaryBytesAndManifestHashThroughHttp) {
    const std::string bytes("a\0\xff\r\n", 5);
    const auto hash = utx::common::sha256_hex(bytes);
    const auto md5 = utx::common::md5_hex(bytes);
    httplib::Server server;
    server.Get("/chain/demo/config", [](const auto&, auto& res) {
        res.set_content(R"({"data":{"projectors":["OwnerProjector","ArtifactProjector"]}})", "application/json");
    });
    server.Get("/chain/demo/state", [&](const auto&, auto& res) {
        nlohmann::json published = {{"version", hash}, {"sha256", hash}, {"total_size", bytes.size()}};
        nlohmann::json data = {{"_utx_state_model", "projector-slices-v1"},
            {"projector_states", nlohmann::json::array({nlohmann::json::object(), {{"published", published}}})}};
        res.set_content(nlohmann::json{{"data", data}}.dump(), "application/json");
    });
    std::string served = bytes;
    server.Get("/render/demo", [&](const auto&, auto& res) {
        res.set_content(served, "application/octet-stream");
    });
    const auto port = server.bind_to_any_port("127.0.0.1");
    ASSERT_GT(port, 0);
    std::jthread worker([&] { server.listen_after_bind(); });
    struct Stop { httplib::Server& s; ~Stop() { s.stop(); } } stop{server};
    const auto target = "127.0.0.1:" + std::to_string(port);
    const auto result = fetch_artifact(target, "demo", md5);
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(*result, bytes);
    EXPECT_FALSE(fetch_artifact(target, "demo", std::string(32, '0')));
    // Change body between separate HTTP requests; no concurrent access.
    served = "xxxxx";
    EXPECT_FALSE(fetch_artifact(target, "demo", md5));
    served = bytes + "extra";
    EXPECT_FALSE(fetch_artifact(target, "demo", md5));
}
} // namespace
