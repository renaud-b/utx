#include <chrono>
#include <atomic>
#include <sstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "infrastructure/inspection/ChainInspection.hpp"
#include "common/HumanReadableSize.hpp"

namespace {
using namespace utx::app::infrastructure::inspection;
using json = nlohmann::json;

constexpr const char* kAlice = "0x1111111111111111111111111111111111111111";
constexpr const char* kBob = "0x2222222222222222222222222222222222222222";

struct TestNode {
    httplib::Server server;
    std::thread worker;
    int port{-1};

    ~TestNode() {
        server.stop();
        if (worker.joinable()) worker.join();
    }

    void start() {
        port = server.bind_to_any_port("127.0.0.1");
        ASSERT_GT(port, 0);
        worker = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }

    std::string endpoint() const {
        return "http://127.0.0.1:" + std::to_string(port);
    }
};

TEST(ChainInspection, ReadsOwnersProjectorsHeightAndPublicProfiles) {
    TestNode node;
    node.server.Get("/chain/demo/config", [](const auto&, auto& res) {
        res.set_content(json{{"data", {
            {"projectors", json::array({"OwnerProjector", "GraphProjector"})},
            {"labels", json::array({"repository"})}
        }}}.dump(), "application/json");
    });
    node.server.Get("/chain/demo/owners", [](const auto&, auto& res) {
        res.set_content(json{{"data", {
            {"chain_id", "demo"},
            {"height", 11},
            {"head_hash", "abcd"},
            {"owners", json::array({kAlice, kBob})}
        }}}.dump(), "application/json");
    });
    node.server.Get(std::string("/chain/") + kAlice + "/state", [](const auto&, auto& res) {
        res.set_content(json{{"data", {
            {"_utx_state_model", "projector-slices-v1"},
            {"projector_states", json::array({
                {{"owners", json::array({kAlice})}},
                {{"graph_data", {{"root", {
                    {"user.pseudo", "Alice"},
                    {"user.description", "Building distributed things"},
                    {"user.picture", "/ipfs/QmAvatarFixture123"},
                    {"owners", json::array({kBob})}
                }}}}}
            })}
        }}}.dump(), "application/json");
    });
    node.start();

    const auto result = fetch_chain_info(node.endpoint(), "demo");
    ASSERT_TRUE(result.has_value()) << result.error();
    ASSERT_TRUE(result->height.has_value());
    EXPECT_EQ(*result->height, 12U);
    EXPECT_EQ(result->projectors, (std::vector<std::string>{"OwnerProjector", "GraphProjector"}));
    EXPECT_EQ(result->labels, (std::vector<std::string>{"repository"}));
    EXPECT_EQ(result->head_hash, "abcd");
    ASSERT_EQ(result->owners.size(), 2U);
    EXPECT_EQ(result->owners[0].address, kAlice);
    EXPECT_TRUE(result->owners[0].available);
    EXPECT_EQ(result->owners[0].pseudo, "Alice");
    EXPECT_EQ(result->owners[0].picture, "/ipfs/QmAvatarFixture123");
    EXPECT_EQ(result->owners[1].address, kBob);
    EXPECT_FALSE(result->owners[1].available);

    std::ostringstream text;
    print_chain_info(*result, text);
    EXPECT_NE(text.str().find("Height     : 12"), std::string::npos);
    EXPECT_NE(text.str().find("GraphProjector"), std::string::npos);
    EXPECT_NE(text.str().find(kAlice), std::string::npos);
    EXPECT_NE(text.str().find("Building distributed things"), std::string::npos);
    EXPECT_NE(text.str().find("Profile     : unavailable"), std::string::npos);
}

TEST(ChainInspection, MalformedConfigurationAndOwnersAreRejected) {
    EXPECT_FALSE(parse_chain_config(json::object(), "demo", "node"));
    EXPECT_FALSE(parse_chain_config(json{
        {"projectors", json::array()}
    }, "demo", "node"));
    EXPECT_FALSE(parse_chain_config(json{
        {"projectors", json::array({"OwnerProjector", "OwnerProjector"})}
    }, "demo", "node"));

    auto info = parse_chain_config(json{
        {"projectors", json::array({"OwnerProjector", "GraphProjector"})}
    }, "demo", "node");
    ASSERT_TRUE(info.has_value()) << info.error();
    EXPECT_FALSE(populate_canonical_owners(*info, json{
        {"chain_id", "wrong"}, {"height", 0}, {"owners", json::array({kAlice})}
    }));
    EXPECT_FALSE(populate_canonical_owners(*info, json{
        {"chain_id", "demo"}, {"height", 0}, {"owners", json::array()}
    }));
    EXPECT_FALSE(populate_canonical_owners(*info, json{
        {"chain_id", "demo"}, {"height", 0}, {"owners", json::array({"not-an-address"})}
    }));
    EXPECT_TRUE(info->owners.empty());

    ASSERT_TRUE(populate_canonical_owners(*info, json{
        {"chain_id", "demo"}, {"height", 0}, {"owners", json::array({kBob})}
    }));
    EXPECT_EQ(info->height, 1U);
    ASSERT_EQ(info->owners.size(), 1U);
    EXPECT_EQ(info->owners[0].address, kBob);
}

TEST(ChainInspection, MissingCanonicalOwnershipFailsClosed) {
    TestNode node;
    node.server.Get("/chain/demo/config", [](const auto&, auto& res) {
        res.set_content(R"({"projectors":["OwnerProjector","GraphProjector"]})",
                        "application/json");
    });
    node.start();
    const auto result = fetch_chain_info(node.endpoint(), "demo");
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("current chain owners"), std::string::npos);
}

TEST(ChainInspection, AmbiguousProfilesCannotOverwriteChainOwnership) {
    OwnerProfile profile{.address = kAlice};
    const json graph = {{"graph_data", {{"root", {{"user.pseudo", "Mallory"}}}}}};
    const json state = {
        {"_utx_state_model", "projector-slices-v1"},
        {"projector_states", json::array({graph, graph})}
    };
    populate_profile(profile, state);
    EXPECT_FALSE(profile.available);
    EXPECT_EQ(profile.address, kAlice);

    populate_profile(profile, json{{"_utx_state_model", "unsupported"}});
    EXPECT_FALSE(profile.available);
}

TEST(ChainInspection, TerminalOutputStripsControlCharactersFromPublicProfile) {
    ChainInfo info{.chain_id = "demo", .endpoint = "http://node"};
    info.owners.push_back(OwnerProfile{
        .address = kAlice,
        .pseudo = "Alice\x1b[2J",
        .description = "Hello\nworld\r\n",
        .available = true
    });
    std::ostringstream output;
    print_chain_info(info, output);
    EXPECT_EQ(output.str().find('\x1b'), std::string::npos);
    EXPECT_NE(output.str().find("Hello world"), std::string::npos);
    EXPECT_FALSE(picture_cid("http://example.com/avatar.png"));
    EXPECT_FALSE(picture_cid("/ipfs/Qmabc/other"));
    EXPECT_FALSE(picture_cid("/ipfs/Qmabc?size=large"));
    EXPECT_FALSE(picture_cid("/ipfs/Qmabc%2fother"));
    EXPECT_EQ(picture_cid("/ipfs/Qmabc"), std::optional<std::string>("Qmabc"));
    EXPECT_EQ(picture_cid("ipfs://bafy123"), std::optional<std::string>("bafy123"));
    EXPECT_EQ(picture_cid("bafy123"), std::optional<std::string>("bafy123"));
}

TEST(ChainInspection, DownloadsAvatarFromSelectedNodeIpfsEndpoint) {
    TestNode node;
    constexpr const char* cid = "QmAvatarPictureFixture123";
    const std::string expected{static_cast<char>(0x89), 'P', 'N', 'G', '\0', 'x'};
    std::atomic<int> requests{0};
    node.server.Get(std::string("/ipfs/") + cid, [&](const auto&, auto& res) {
        ++requests;
        res.set_content(expected, "image/png");
    });
    node.start();

    const auto fetched = fetch_ipfs_avatar(node.endpoint(), std::string("/ipfs/") + cid);
    ASSERT_TRUE(fetched.has_value()) << fetched.error();
    EXPECT_EQ(*fetched, expected);
    EXPECT_EQ(requests.load(), 1);
}

TEST(ChainInspection, AvatarNetworkErrorsAndSizeLimitAreNonFatal) {
    TestNode node;
    std::atomic<int> requests{0};
    node.server.Get("/ipfs/QmOversize", [&](const auto&, auto& res) {
        ++requests;
        res.set_content(std::string(2 * 1024 * 1024 + 1, 'x'), "image/png");
    });
    node.start();

    EXPECT_FALSE(fetch_ipfs_avatar(node.endpoint(), "/ipfs/../escape"));
    EXPECT_FALSE(fetch_ipfs_avatar(node.endpoint(), "https://example.com/avatar.png"));
    EXPECT_EQ(requests.load(), 0);
    EXPECT_FALSE(fetch_ipfs_avatar(node.endpoint(), "/ipfs/QmMissing"));
    EXPECT_FALSE(fetch_ipfs_avatar(node.endpoint(), "/ipfs/QmOversize"));
    EXPECT_EQ(requests.load(), 1);
}

TEST(ChainInspection, FileSizesAreHumanReadable) {
    using utx::common::human_readable_bytes;
    EXPECT_EQ(human_readable_bytes(0), "0 B");
    EXPECT_EQ(human_readable_bytes(1), "1 B");
    EXPECT_EQ(human_readable_bytes(1023), "1023 B");
    EXPECT_EQ(human_readable_bytes(1024), "1.0 KiB");
    EXPECT_EQ(human_readable_bytes(1536), "1.5 KiB");
    EXPECT_EQ(human_readable_bytes(1024 * 1024), "1.0 MiB");
    EXPECT_EQ(human_readable_bytes(5ULL * 1024 * 1024 * 1024), "5.0 GiB");
}

TEST(ChainInspection, ConsentIsExplicitAndFailClosed) {
    std::istringstream unavailable;
    std::ostringstream silent;
    EXPECT_TRUE(download_approved(true, false, unavailable, silent));
    EXPECT_TRUE(download_approved(false, true, unavailable, silent));
    EXPECT_TRUE(silent.str().empty());
    EXPECT_FALSE(download_approved(false, false, unavailable, silent));
    EXPECT_NE(silent.str().find("[y/N]"), std::string::npos);
    for (const std::string answer : {"y\n", "YES\n", " yes \n"}) {
        std::istringstream in(answer);
        std::ostringstream out;
        EXPECT_TRUE(confirm_download(in, out));
    }
    for (const std::string answer : {"\n", "n\n", "no\n", "yes please\n", ""}) {
        std::istringstream in(answer);
        std::ostringstream out;
        EXPECT_FALSE(confirm_download(in, out));
    }
}
} // namespace
