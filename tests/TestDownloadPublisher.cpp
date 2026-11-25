#include <atomic>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <thread>
#include <gtest/gtest.h>
#include <httplib.h>
#include "infrastructure/download/PublisherIdentity.hpp"
#include "infrastructure/download/TerminalAvatar.hpp"
#include "use_case/DownloadCommand.hpp"

namespace {
namespace fs = std::filesystem;
using namespace utx::app::infrastructure::download;
using utx::app::infrastructure::context::AppContext;
using utx::app::use_case::DownloadCommand;
using json = nlohmann::json;

TEST(PublisherIdentity, SanitizesTerminalEscapesAndPreservesUtf8) {
    const std::string unsafe = "Alice\033]52;payload\a\n" "\xc2\x9b" "\xe2\x80\xae" "caf\xc3\xa9";
    const auto safe = terminal_text(unsafe, 100);
    EXPECT_EQ(safe.find('\033'), std::string::npos);
    EXPECT_EQ(safe.find('\n'), std::string::npos);
    EXPECT_EQ(safe.find("\xc2\x9b"), std::string::npos);
    EXPECT_EQ(safe.find("\xe2\x80\xae"), std::string::npos);
    EXPECT_NE(safe.find("caf\xc3\xa9"), std::string::npos);
    EXPECT_EQ(terminal_text("abcdef", 3), "abc...");
}

TEST(PublisherIdentity, AvatarOnlyUsesPublicCidAndBoundedPngDecoder) {
    EXPECT_TRUE(public_picture_path("/ipfs/Qm123456789"));
    EXPECT_FALSE(public_picture_path("https://evil.test/avatar.png"));
    EXPECT_FALSE(public_picture_path("/ipfs/../escape"));
    EXPECT_FALSE(public_picture_path("/ipfs/Qm123456789?x=1"));
    EXPECT_FALSE(png_terminal_preview("not an image"));
#ifdef UTX_HAS_PNG
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = 1;
    image.height = 1;
    image.format = PNG_FORMAT_RGBA;
    const unsigned char pixel[] = {255, 0, 0, 255};
    png_alloc_size_t size = 0;
    ASSERT_TRUE(png_image_write_to_memory(&image, nullptr, &size, 0, pixel, 0, nullptr));
    std::string bytes(size, '\0');
    ASSERT_TRUE(png_image_write_to_memory(&image, bytes.data(), &size, 0, pixel, 0, nullptr));
    bytes.resize(size);
    const auto preview = png_terminal_preview(bytes);
    ASSERT_TRUE(preview.has_value());
    EXPECT_NE(preview->find("\033[38;2;255;0;0m"), std::string::npos);
    EXPECT_NE(preview->find("\xe2\x96\x80"), std::string::npos);
    png_image_free(&image);
#endif
}

TEST(PublisherIdentity, DefaultNoAndExplicitYes) {
    std::ostringstream output;
    for (const auto* answer : {"", "\n", "no\n", "y something\n"}) {
        std::istringstream input(answer);
        EXPECT_FALSE(confirm_download(input, output));
    }
    std::istringstream yes("y\n");
    EXPECT_TRUE(confirm_download(yes, output));
}

class PublisherDownload : public testing::Test {
protected:
    fs::path root = fs::temp_directory_path() / ("utx-publisher-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    httplib::Server server;
    std::jthread worker;
    std::atomic<int> owner_reads{0}, file_reads{0};
    std::atomic<bool> change_head{false}, malformed{false};
    std::string endpoint;
    const std::string owner = "0x" + std::string(40, 'a');
    const std::string head = std::string(64, 'b');

    void SetUp() override {
        fs::create_directories(root);
        server.Get("/chain/manifest/owners", [&](const auto&, auto& response) {
            const auto count = ++owner_reads;
            json data = {{"chain_id", malformed ? "other" : "manifest"},
                {"height", 1}, {"head_hash", change_head && count >= 3 ? std::string(64, 'c') : head},
                {"owners", json::array({owner})}};
            response.set_content(json{{"data", data}}.dump(), "application/json");
        });
        server.Get("/graph/manifest", [&](const auto& req, auto& res) {
            if (!req.has_param("block_hash") || req.get_param_value("block_hash") != head) {
                res.status = 400; return;
            }
            res.set_content(R"({"graph_id":"manifest","root":{"name":"root","children":[{"name":"version","value":"1"},{"name":"targets","children":[{"name":"0","children":[{"name":"path","value":"tests/a.cjs"},{"name":"chain","value":"file"},{"name":"kind","value":"graph"}]}]}]}})", "application/json");
        });
        server.Get("/graph/" + owner, [&](const auto&, auto& res) {
            json graph = {{"graph_id", owner}, {"root", {
                {"name", "root"}, {"user.pseudo", "Alice"}, {"user.description", "Developer"}
            }}};
            res.set_content(graph.dump(), "application/json");
        });
        server.Get("/graph/file", [&](const auto&, auto& res) {
            ++file_reads;
            res.set_content(R"({"graph_id":"file","root":{"name":"root","value":"test"}})", "application/json");
        });
        const auto port = server.bind_to_any_port("127.0.0.1");
        ASSERT_GT(port, 0);
        endpoint = "127.0.0.1:" + std::to_string(port);
        worker = std::jthread([&] { server.listen_after_bind(); });
    }
    void TearDown() override {
        server.stop();
        if (worker.joinable()) worker.join();
        fs::remove_all(root);
    }
    int run(std::istream& in, std::ostream& out, std::vector<std::string> extra = {}) {
        AppContext ctx;
        ctx.root = root;
        ctx.network_client.target = endpoint;
        ctx.project_config.api_target = endpoint;
        std::vector<std::string> args{"utx", "download", "manifest"};
        args.insert(args.end(), extra.begin(), extra.end());
        return DownloadCommand(std::move(ctx), in, out).execute(args);
    }
};

TEST_F(PublisherDownload, DeclineDisplaysIdentityAndDoesNotWriteOrFetchFiles) {
    std::istringstream input("\n");
    std::ostringstream output;
    EXPECT_EQ(run(input, output), 0);
    EXPECT_NE(output.str().find(owner), std::string::npos);
    EXPECT_NE(output.str().find("Alice"), std::string::npos);
    EXPECT_NE(output.str().find("Developer"), std::string::npos);
    EXPECT_NE(output.str().find("Download this project?"), std::string::npos);
    EXPECT_EQ(file_reads.load(), 0);
    EXPECT_TRUE(fs::is_empty(root));
}
TEST_F(PublisherDownload, ChangedHeadAfterConfirmationDoesNotWrite) {
    change_head = true;
    std::istringstream input("y\n");
    std::ostringstream output;
    EXPECT_NE(run(input, output), 0);
    EXPECT_EQ(file_reads.load(), 0);
    EXPECT_TRUE(fs::is_empty(root));
}
TEST_F(PublisherDownload, MismatchedOwnerResponseFailsClosed) {
    malformed = true;
    std::istringstream input("y\n");
    std::ostringstream output;
    EXPECT_NE(run(input, output), 0);
    EXPECT_EQ(file_reads.load(), 0);
    EXPECT_TRUE(fs::is_empty(root));
}
TEST_F(PublisherDownload, ExplicitYesRestoresProjectWithoutPrompt) {
    std::istringstream input;
    std::ostringstream output;
    EXPECT_EQ(run(input, output, {"--yes"}), 0);
    EXPECT_EQ(output.str().find("Download this project?"), std::string::npos);
    EXPECT_EQ(file_reads.load(), 1);
    EXPECT_TRUE(fs::exists(root / "tests/a.cjs"));
    EXPECT_TRUE(fs::exists(root / ".utx.deploy.json"));
    EXPECT_TRUE(fs::exists(root / ".utx/config.json"));
}
TEST_F(PublisherDownload, DryRunNeverWritesOrPrompts) {
    std::istringstream input;
    std::ostringstream output;
    EXPECT_EQ(run(input, output, {"--dry-run"}), 0);
    EXPECT_EQ(output.str().find("Download this project?"), std::string::npos);
    EXPECT_EQ(file_reads.load(), 1);
    EXPECT_TRUE(fs::is_empty(root));
}
} // namespace
