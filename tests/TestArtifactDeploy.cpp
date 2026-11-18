#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "common/Base64.hpp"
#include "common/Hash.hpp"
#include "common/Uuid.hpp"
#include "domain/Types.hpp"
#include "infrastructure/context/AppContext.hpp"
#include "infrastructure/deploy_config/ArtifactMime.hpp"
#include "infrastructure/deploy_config/ArtifactPlanVerifier.hpp"
#include "infrastructure/deploy_config/ArtifactTransport.hpp"
#include "infrastructure/deploy_config/DeployClient.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"
#include "infrastructure/deploy_config/StagingManager.hpp"
#include "infrastructure/wallet/WalletHelper.hpp"
#include "use_case/AddCommand.hpp"
#include "use_case/DeployCommand.hpp"

namespace {
using nlohmann::json;
using utx::app::domain::TargetKind;
using utx::app::domain::DeployRequest;
using utx::app::infrastructure::context::AppContext;
using utx::app::infrastructure::deploy::DeployClient;
using utx::app::infrastructure::deploy::decode_planned_payload;
using utx::app::infrastructure::deploy::decode_artifact_base64;
using utx::app::infrastructure::deploy::signed_transaction_from_json;
using utx::app::infrastructure::deploy::artifact_mime_for_path;
using utx::app::infrastructure::deploy::verify_artifact_plan;
using utx::infra::wallet::WalletHelper;

struct TemporaryProject {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("utx-artifact-cli-" + utx::common::generate_uuid_v7().to_string());

    TemporaryProject() {
        std::filesystem::create_directories(root);
    }
    ~TemporaryProject() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    void put(const std::string& path, const std::string& content) const {
        const auto dest = root / path;
        std::filesystem::create_directories(dest.parent_path());
        std::ofstream f(dest, std::ios::binary);
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
};

class LocalArtifactNode {
public:
    httplib::Server server;
    ~LocalArtifactNode() {
        server.stop();
        if (thread_.joinable()) thread_.join();
    }
    void start() {
        const int port = server.bind_to_any_port("127.0.0.1");
        if (port <= 0) throw std::runtime_error("Cannot bind artifact test node");
        url_ = "http://127.0.0.1:" + std::to_string(port);
        thread_ = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    const std::string& url() const { return url_; }
private:
    std::string url_;
    std::thread thread_;
};

constexpr std::size_t kChunk = 24 * 1024;
std::string artifact_chunk(
    const std::string& content, const std::string& mime,
    const std::size_t index) {
    const auto count = std::max<std::size_t>(
        1, (content.size() + kChunk - 1) / kChunk);
    const auto offset = index * kChunk;
    const auto length = std::min(kChunk, content.size() - offset);
    json message = {
        {"version", utx::common::sha256_hex(content)},
        {"index", index},
        {"bytes", json::binary(std::vector<std::uint8_t>(
            content.begin() + static_cast<std::ptrdiff_t>(offset),
            content.begin() + static_cast<std::ptrdiff_t>(offset + length)))}
    };
    if (index == 0) {
        message["metadata"] = {
            {"chunk_count", count}, {"total_size", content.size()},
            {"sha256", utx::common::sha256_hex(content)}, {"mime", mime}
        };
    }
    const auto packed = json::to_msgpack(message);
    return std::string(utx::app::infrastructure::deploy::kArtifactChunkPrefix) +
        std::string(packed.begin(), packed.end());
}

json artifact_plan(
    const std::string& content, const std::string& mime,
    const bool new_chain) {
    json transactions = json::array();
    if (new_chain) {
        const auto genesis = json{
            {"projectors", json::array({"OwnerProjector", "ArtifactProjector"})},
            {"labels", json::array()}
        };
        const auto packed = json::to_msgpack(genesis);
        transactions.push_back({
            {"payload_data", utx::common::base64::encode(
                std::string(packed.begin(), packed.end()))}
        });
    }
    const auto count = std::max<std::size_t>(
        1, (content.size() + kChunk - 1) / kChunk);
    for (std::size_t i = 0; i < count; ++i) {
        transactions.push_back({
            {"payload_data_b64",
             utx::common::base64::encode(artifact_chunk(content, mime, i))}
        });
    }
    return {
        {"plan_id", "artifact-plan-test"},
        {"strategy", "artifact"},
        {"content_hash", utx::common::sha256_hex(content)},
        {"ring_reference", json{{"version", "test-ring"}}},
        {"transactions", transactions}
    };
}
} // namespace

TEST(ArtifactDeployTest, BinaryPayloadCanBeSignedWithoutBase64InPreimage) {
    const std::string payload = artifact_chunk(
        std::string("\x00\xff\x01", 3), "application/octet-stream", 0);
    const auto decoded = decode_artifact_base64(
        utx::common::base64::encode(payload));
    ASSERT_TRUE(decoded);
    EXPECT_EQ(*decoded, payload);

    auto wallet = WalletHelper::generate_keypair();
    const auto signed_tx = DeployClient::build_signed_tx(payload, "chain", wallet);
    ASSERT_TRUE(signed_tx) << (signed_tx ? "" : signed_tx.error());
    EXPECT_EQ(signed_tx->data, payload);

    const auto transport =
        utx::app::infrastructure::deploy::signed_transaction_for_json(
            *signed_tx, true);
    EXPECT_FALSE(transport.contains("data"));
    EXPECT_TRUE(transport.contains("data_b64"));
    EXPECT_NO_THROW((void)transport.dump());
    const auto restored = signed_transaction_from_json(transport);
    ASSERT_TRUE(restored) << (restored ? "" : restored.error());
    EXPECT_EQ(restored->data, payload);
    EXPECT_EQ(restored->serialize_for_signing(),
              signed_tx->serialize_for_signing());

    auto ambiguous = transport;
    ambiguous["data"] = "wrong";
    EXPECT_FALSE(signed_transaction_from_json(ambiguous));
    auto invalid = transport;
    invalid["data_b64"] = "%%%";
    EXPECT_FALSE(signed_transaction_from_json(invalid));
}

TEST(ArtifactDeployTest, RejectsPlanChunksDifferentFromLocalFile) {
    const std::string content(32 * 1024, 'A');
    auto plan = artifact_plan(content, "application/octet-stream", true);
    EXPECT_TRUE(verify_artifact_plan(plan, content, "application/octet-stream"));

    EXPECT_FALSE(verify_artifact_plan(plan, content, "image/png"));
    EXPECT_FALSE(verify_artifact_plan(plan, content + "x",
                                      "application/octet-stream"));

    const auto forged = artifact_chunk(
        std::string(32 * 1024, 'B'), "application/octet-stream", 1);
    plan["transactions"][2]["payload_data_b64"] =
        utx::common::base64::encode(forged);
    EXPECT_FALSE(verify_artifact_plan(plan, content, "application/octet-stream"));

    plan["transactions"][2]["payload_data_b64"] = "%%%";
    EXPECT_FALSE(verify_artifact_plan(plan, content, "application/octet-stream"));
}

TEST(ArtifactDeployTest, PublishesBinaryChunksThroughExistingSignedDeployWorkflow) {
    LocalArtifactNode http;
    std::string content(30 * 1024, '\0');
    for (std::size_t i = 0; i < content.size(); ++i) {
        content[i] = static_cast<char>(i & 0xff);
    }
    const auto expected = artifact_plan(content, "application/javascript", true);
    json captured_prepare;
    json captured_submit;

    http.server.Post("/api/deploy/prepare",
        [&](const httplib::Request& request, httplib::Response& response) {
            captured_prepare = json::parse(request.body);
            response.set_content(expected.dump(), "application/json");
        });
    http.server.Post("/api/deploy/submit",
        [&](const httplib::Request& request, httplib::Response& response) {
            captured_submit = json::parse(request.body);
            response.status = 202;
            response.set_content(json{
                {"status", "queued"},
                {"pending_block_ids",
                 json::array({"pending-genesis", "pending-0", "pending-1"})}
            }.dump(), "application/json");
        });
    http.server.Get(R"(/chain/([^/]+)/pending/([^/]+))",
        [](const httplib::Request& request, httplib::Response& response) {
            response.set_content(json{
                {"id", request.matches[2].str()},
                {"chain_address", request.matches[1].str()},
                {"state", "Finalized"}, {"error", nullptr}
            }.dump(), "application/json");
        });
    http.start();

    const auto wallet = WalletHelper::generate_keypair();
    DeployClient client(http.url());
    DeployRequest req;
    req.chain_id = "artifact-chain";
    req.file_path = "dist/app.js";
    req.kind = "artifact";
    req.mime = "application/javascript";
    req.content = content;
    req.commit_message = "binary asset";

    const auto published = client.deploy(req, wallet);
    ASSERT_TRUE(published) << (published ? "" : published.error());
    EXPECT_TRUE(published->success);
    ASSERT_FALSE(captured_prepare.is_null());
    EXPECT_EQ(captured_prepare.at("kind"), "artifact");
    EXPECT_EQ(captured_prepare.at("mime"), "application/javascript");
    EXPECT_FALSE(captured_prepare.contains("content"));
    EXPECT_EQ(utx::common::base64::decode(
        captured_prepare.at("content_b64").get<std::string>()), content);

    ASSERT_FALSE(captured_submit.is_null());
    const auto& txs = captured_submit.at("signed_transactions");
    ASSERT_EQ(txs.size(), 3U);
    EXPECT_TRUE(txs[0].contains("data")); // existing genesis envelope
    EXPECT_FALSE(txs[0].contains("data_b64"));
    for (std::size_t i = 1; i < txs.size(); ++i) {
        EXPECT_FALSE(txs[i].contains("data"));
        EXPECT_TRUE(txs[i].contains("data_b64"));
        const auto decoded_tx = signed_transaction_from_json(txs.at(i));
        ASSERT_TRUE(decoded_tx);
        const auto planned = decode_planned_payload(
            expected.at("transactions").at(i));
        ASSERT_TRUE(planned);
        EXPECT_EQ(decoded_tx->data, planned->bytes);
        EXPECT_EQ(decoded_tx->sender.to_string(), wallet.address);
        EXPECT_FALSE(decoded_tx->signature.to_string().empty());
    }
}

TEST(ArtifactDeployTest, BadNodePlanDoesNotReachSubmit) {
    LocalArtifactNode http;
    const std::string content = "real contents";
    std::atomic<int> submit_calls{0};
    auto forged = artifact_plan(
        "other contents", "application/octet-stream", false);
    http.server.Post("/api/deploy/prepare",
        [&](const httplib::Request&, httplib::Response& response) {
            response.set_content(forged.dump(), "application/json");
        });
    http.server.Post("/api/deploy/submit",
        [&](const httplib::Request&, httplib::Response& response) {
            ++submit_calls;
            response.status = 500;
        });
    http.start();

    DeployRequest req;
    req.chain_id = "artifact-chain";
    req.kind = "artifact";
    req.content = content;
    const auto result = DeployClient(http.url()).deploy(
        req, WalletHelper::generate_keypair());
    EXPECT_FALSE(result);
    EXPECT_EQ(submit_calls.load(), 0);
}

TEST(ArtifactDeployTest, AddStagesArtifactKindWithoutChangingJsSourceDefault) {
    TemporaryProject project;
    project.put("dist/app.js", "console.log('compiled');");
    project.put("dist/logo.png", std::string("\x89PNG\x00\xff", 6));
    AppContext context;
    context.root = project.root;

    EXPECT_EQ(utx::app::use_case::AddCommand(context).execute(
        {"utx", "add", (project.root / "dist/app.js").string(), "--kind", "artifact"}), 0);
    EXPECT_EQ(utx::app::use_case::AddCommand(context).execute(
        {"utx", "add", (project.root / "dist/logo.png").string()}), 0);

    utx::app::infrastructure::deploy::DeployConfigManager manager;
    const auto state = manager.load_deploy_config(project.root);
    ASSERT_TRUE(state);
    ASSERT_EQ(state->targets.size(), 2U);
    for (const auto& target : state->targets) {
        EXPECT_EQ(target.kind, TargetKind::Artifact);
    }
    EXPECT_EQ(artifact_mime_for_path("dist/app.js"), "application/javascript");
    EXPECT_EQ(artifact_mime_for_path("dist/logo.png"), "image/png");
    EXPECT_EQ(artifact_mime_for_path("random.bin"), "application/octet-stream");

    EXPECT_EQ(utx::app::use_case::AddCommand(context).execute(
        {"utx", "add", (project.root / "dist/app.js").string(), "--kind", "nonesuch"}), 1);
}

TEST(ArtifactDeployTest, EmptyAndOpaqueFileHaveCorrectChunkEncoding) {
    const std::string empty;
    const auto plan = artifact_plan(empty, "application/octet-stream", true);
    EXPECT_TRUE(verify_artifact_plan(plan, empty, "application/octet-stream"));

    const std::string binary("\x00\xff\xfe\x01", 4);
    EXPECT_TRUE(verify_artifact_plan(
        artifact_plan(binary, "application/octet-stream", false),
        binary, "application/octet-stream"));
}
