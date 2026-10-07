#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "common/Uuid.hpp"
#include "infrastructure/deploy_config/DeployClient.hpp"
#include "infrastructure/wallet/WalletHelper.hpp"

namespace {

using namespace std::chrono_literals;
using utx::app::domain::DeployRequest;
using utx::app::infrastructure::deploy::DeployClient;
using utx::infra::wallet::WalletHelper;

class LocalHttpServer {
public:
    httplib::Server server;

    LocalHttpServer() = default;
    LocalHttpServer(const LocalHttpServer&) = delete;
    LocalHttpServer& operator=(const LocalHttpServer&) = delete;

    ~LocalHttpServer() {
        server.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void start() {
        port_ = server.bind_to_any_port("127.0.0.1");
        if (port_ <= 0) {
            throw std::runtime_error("failed to bind local HTTP test server");
        }

        thread_ = std::thread([this] {
            server.listen_after_bind();
        });

        server.wait_until_ready();
    }

    [[nodiscard]]
    std::string base_url() const {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

private:
    int port_{-1};
    std::thread thread_;
};

TEST(DeployClientTest, DeploySubmitsRingReferenceAndWaitsForFinalization) {
    LocalHttpServer http;

    const nlohmann::json ring_reference = {
        {"version", "ring-v1"}
    };

    std::mutex captured_mutex;
    nlohmann::json captured_submit;
    std::atomic<int> pending_calls{0};

    http.server.Post(
        "/api/deploy/prepare",
        [&](const httplib::Request& req, httplib::Response& res) {
            const auto request = nlohmann::json::parse(req.body);

            EXPECT_EQ(request.at("chain_id"), "chain-123");
            EXPECT_EQ(request.at("content"), "hello");
            EXPECT_EQ(request.at("kind"), "identity");
            EXPECT_EQ(request.at("projector"), "IdentityProjector");

            res.set_content(
                nlohmann::json{
                    {"plan_id", "plan-123"},
                    {"ring_reference", ring_reference},
                    {"transactions", nlohmann::json::array({
                        {
                            {"payload_data", "urn:pi:test:payload"}
                        }
                    })}
                }.dump(),
                "application/json"
            );
        }
    );

    http.server.Post(
        "/api/deploy/submit",
        [&](const httplib::Request& req, httplib::Response& res) {
            {
                std::lock_guard lock(captured_mutex);
                captured_submit = nlohmann::json::parse(req.body);
            }

            res.status = 202;
            res.set_content(
                nlohmann::json{
                    {"status", "queued"},
                    {"pending_block_ids", nlohmann::json::array({"pending-1"})},
                    {"transactions_queued", 1}
                }.dump(),
                "application/json"
            );
        }
    );

    http.server.Get(
        R"(/chain/([^/]+)/pending/([^/]+))",
        [&](const httplib::Request& req, httplib::Response& res) {
            ++pending_calls;

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

    const auto wallet = WalletHelper::generate_keypair();
    DeployClient client(http.base_url());

    DeployRequest request;
    request.chain_id = "chain-123";
    request.file_path = "index.html";
    request.content = "hello";
    request.kind = "identity";
    request.projector = "IdentityProjector";
    request.commit_message = "test deploy";

    const auto result = client.deploy(request, wallet);

    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_TRUE(result->success);
    EXPECT_EQ(result->plan_id, "plan-123");
    EXPECT_GE(pending_calls.load(), 1);

    nlohmann::json submit;
    {
        std::lock_guard lock(captured_mutex);
        submit = captured_submit;
    }

    ASSERT_FALSE(submit.is_null());
    EXPECT_EQ(submit.at("plan_id"), "plan-123");
    EXPECT_EQ(submit.at("chain_id"), "chain-123");
    EXPECT_EQ(submit.at("ring_reference"), ring_reference);

    const auto& signed_transactions =
        submit.at("signed_transactions");

    ASSERT_TRUE(signed_transactions.is_array());
    ASSERT_EQ(signed_transactions.size(), 1);

    const auto& signed_tx = signed_transactions.at(0);
    EXPECT_EQ(signed_tx.at("sender"), wallet.address);
    EXPECT_EQ(signed_tx.at("receiver"), "chain-123");
    EXPECT_EQ(signed_tx.at("data"), "urn:pi:test:payload");
    EXPECT_EQ(signed_tx.at("sender_pubkey"), wallet.public_key_hex);
    EXPECT_FALSE(
        signed_tx.at("signature").get<std::string>().empty()
    );
}


TEST(DeployClientTest, DurableSubmissionReusesExactSignedTransactionsAfterClientRecreation) {
    LocalHttpServer http;

    const nlohmann::json ring_reference = {
        {"version", "ring-v1"}
    };

    std::mutex bodies_mutex;
    std::vector<nlohmann::json> submit_bodies;
    std::atomic<int> submit_calls{0};

    http.server.Post(
        "/api/deploy/prepare",
        [&](const httplib::Request&, httplib::Response& res) {
            res.set_content(
                nlohmann::json{
                    {"plan_id", "plan-durable-retry"},
                    {"ring_reference", ring_reference},
                    {"transactions", nlohmann::json::array({
                        {
                            {"payload_data", "urn:pi:test:durable"}
                        }
                    })}
                }.dump(),
                "application/json"
            );
        }
    );

    http.server.Post(
        "/api/deploy/submit",
        [&](const httplib::Request& req, httplib::Response& res) {
            {
                std::lock_guard lock(bodies_mutex);
                submit_bodies.push_back(
                    nlohmann::json::parse(req.body)
                );
            }

            const auto call = ++submit_calls;
            if (call == 1) {
                res.status = 400;
                res.set_content(
                    R"({"error":"deploy_failed","message":"scripted"})",
                    "application/json"
                );
                return;
            }

            res.status = 202;
            res.set_content(
                nlohmann::json{
                    {"status", "queued"},
                    {"pending_block_ids", nlohmann::json::array({"pending-durable"})}
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

    const auto root =
        std::filesystem::temp_directory_path() /
        ("utx_deploy_submission_" +
         utx::common::generate_uuid_v7().to_string());
    std::filesystem::create_directories(root);

    const auto wallet = WalletHelper::generate_keypair();

    DeployRequest request;
    request.chain_id = "chain-durable";
    request.file_path = "index.html";
    request.content = "hello";
    request.kind = "identity";
    request.commit_message = "test durable retry";

    {
        DeployClient first(http.base_url(), root);
        const auto failed = first.deploy(request, wallet);
        ASSERT_FALSE(failed.has_value());
    }

    {
        DeployClient second(http.base_url(), root);
        const auto retried = second.deploy(request, wallet);
        ASSERT_TRUE(retried.has_value()) << retried.error();
    }

    std::vector<nlohmann::json> captured;
    {
        std::lock_guard lock(bodies_mutex);
        captured = submit_bodies;
    }

    ASSERT_EQ(captured.size(), 2U);
    EXPECT_EQ(
        captured[0].at("signed_transactions"),
        captured[1].at("signed_transactions")
    );

    const auto pending_dir =
        root / ".utx" / "pending-deploy-submissions";
    if (std::filesystem::exists(pending_dir)) {
        EXPECT_TRUE(std::filesystem::is_empty(pending_dir));
    }

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST(DeployClientTest, SubmitRetriesTransientFailureWithExactSameBody) {
    LocalHttpServer http;

    std::mutex bodies_mutex;
    std::vector<std::string> bodies;
    std::atomic<int> submit_calls{0};

    http.server.Post(
        "/api/deploy/submit",
        [&](const httplib::Request& req, httplib::Response& res) {
            {
                std::lock_guard lock(bodies_mutex);
                bodies.push_back(req.body);
            }

            const auto call = ++submit_calls;
            if (call == 1) {
                res.status = 503;
                res.set_content(
                    R"({"error":"deploy_gate_unavailable"})",
                    "application/json"
                );
                return;
            }

            res.status = 202;
            res.set_content(
                nlohmann::json{
                    {"status", "queued"},
                    {"pending_block_ids", nlohmann::json::array({"pending-retry"})}
                }.dump(),
                "application/json"
            );
        }
    );

    http.start();

    DeployClient client(http.base_url());

    const nlohmann::json signed_txs =
        nlohmann::json::array({
            {
                {"sender", "sender"},
                {"receiver", "chain"},
                {"amount", 0},
                {"nonce", 42},
                {"data", "payload"},
                {"sender_pubkey", "pubkey"},
                {"signature", "signature"}
            }
        });

    const auto result = client.submit(
        "plan-retry",
        "chain",
        nlohmann::json{{"version", "ring-v1"}},
        signed_txs
    );

    ASSERT_TRUE(result.has_value()) << result.error();
    ASSERT_EQ(submit_calls.load(), 2);

    std::vector<std::string> captured;
    {
        std::lock_guard lock(bodies_mutex);
        captured = bodies;
    }

    ASSERT_EQ(captured.size(), 2U);
    EXPECT_EQ(captured[0], captured[1]);
}

TEST(DeployClientTest, SubmitDoesNotRetryDeterministicClientFailure) {
    LocalHttpServer http;
    std::atomic<int> submit_calls{0};

    http.server.Post(
        "/api/deploy/submit",
        [&](const httplib::Request&, httplib::Response& res) {
            ++submit_calls;
            res.status = 400;
            res.set_content(
                R"({"error":"deploy_failed","message":"submission_mismatch"})",
                "application/json"
            );
        }
    );

    http.start();

    DeployClient client(http.base_url());

    const auto result = client.submit(
        "plan-no-retry",
        "chain",
        nlohmann::json{{"version", "ring-v1"}},
        nlohmann::json::array()
    );

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(submit_calls.load(), 1);
}

TEST(DeployClientTest, PrepareSendsProjectorComposition) {
    LocalHttpServer http;
    nlohmann::json captured;

    http.server.Post(
        "/api/deploy/prepare",
        [&](const httplib::Request& req, httplib::Response& res) {
            captured = nlohmann::json::parse(req.body);
            res.set_content(
                nlohmann::json{
                    {"plan_id", "plan-projectors"},
                    {"transactions", nlohmann::json::array()}
                }.dump(),
                "application/json"
            );
        }
    );

    http.start();

    DeployClient client(http.base_url());

    DeployRequest request;
    request.chain_id = "identity-chain";
    request.kind = "identity";
    request.content = R"({"user":{"pseudo":"renaud"}})";
    request.projectors = {
        "OwnerProjector",
        "DecentralizedProjector@capability-chain",
        "IdentityProjector"
    };

    const auto result = client.prepare(request, "sender");

    ASSERT_TRUE(result.has_value()) << result.error();
    ASSERT_FALSE(captured.is_null());
    EXPECT_EQ(
        captured.at("projectors"),
        (nlohmann::json::array({
            "OwnerProjector",
            "DecentralizedProjector@capability-chain",
            "IdentityProjector"
        }))
    );
    EXPECT_EQ(captured.at("kind"), "identity");
    EXPECT_FALSE(captured.contains("projector"));
}


TEST(DeployClientTest, RawDeployPreservesPayloadAndUsesV1Flow) {
    LocalHttpServer http;

    const nlohmann::json ring_reference = {
        {"version", "ring-v1"}
    };
    const std::string payload =
        "urn:pi:capability:init:family-v1";

    nlohmann::json captured_submit;

    http.server.Post(
        "/api/deploy/prepare",
        [&](const httplib::Request& req, httplib::Response& res) {
            const auto request = nlohmann::json::parse(req.body);

            EXPECT_EQ(request.at("chain_id"), "identity-chain");
            EXPECT_EQ(request.at("kind"), "raw");
            EXPECT_EQ(request.at("content"), payload);

            res.set_content(
                nlohmann::json{
                    {"plan_id", "plan-raw"},
                    {"ring_reference", ring_reference},
                    {"transactions", nlohmann::json::array({
                        {{"payload_data", payload}}
                    })}
                }.dump(),
                "application/json"
            );
        }
    );

    http.server.Post(
        "/api/deploy/submit",
        [&](const httplib::Request& req, httplib::Response& res) {
            captured_submit = nlohmann::json::parse(req.body);
            res.status = 202;
            res.set_content(
                nlohmann::json{
                    {"status", "queued"},
                    {"pending_block_ids", nlohmann::json::array({"pending-raw"})},
                    {"transactions_queued", 1}
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

    const auto wallet = WalletHelper::generate_keypair();
    DeployClient client(http.base_url());

    DeployRequest request;
    request.chain_id = "identity-chain";
    request.file_path = "raw";
    request.kind = "raw";
    request.content = payload;
    request.commit_message = "Raw transaction";
    request.force_snapshot = false;

    const auto result = client.deploy(request, wallet);

    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_TRUE(result->success);
    ASSERT_FALSE(captured_submit.is_null());

    const auto& signed_transactions =
        captured_submit.at("signed_transactions");
    ASSERT_EQ(signed_transactions.size(), 1U);
    EXPECT_EQ(
        signed_transactions.at(0).at("sender"),
        wallet.address
    );
    EXPECT_EQ(
        signed_transactions.at(0).at("receiver"),
        "identity-chain"
    );
    EXPECT_EQ(
        signed_transactions.at(0).at("data"),
        payload
    );
}

TEST(DeployClientTest, EmptyPlanCompletesWithoutSubmit) {
    LocalHttpServer http;
    std::atomic<int> submit_calls{0};

    http.server.Post(
        "/api/deploy/prepare",
        [](const httplib::Request&, httplib::Response& res) {
            res.set_content(
                nlohmann::json{
                    {"plan_id", "plan-empty"},
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

    const auto wallet = WalletHelper::generate_keypair();
    DeployClient client(http.base_url());

    DeployRequest request;
    request.chain_id = "chain-empty";
    request.content = "already current";

    const auto result = client.deploy(request, wallet);

    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_TRUE(result->success);
    EXPECT_EQ(result->plan_id, "plan-empty");
    EXPECT_EQ(submit_calls.load(), 0);
}

TEST(DeployClientTest, RejectedPendingBlockIsTerminalFailure) {
    LocalHttpServer http;

    http.server.Get(
        R"(/chain/([^/]+)/pending/([^/]+))",
        [](const httplib::Request& req, httplib::Response& res) {
            res.set_content(
                nlohmann::json{
                    {"id", req.matches[2].str()},
                    {"chain_address", req.matches[1].str()},
                    {"state", "Rejected"},
                    {
                        "error",
                        {
                            {"message", "invalid attestation"}
                        }
                    }
                }.dump(),
                "application/json"
            );
        }
    );

    http.start();

    DeployClient client(http.base_url());

    const auto result = client.wait_for_finalization(
        "chain-rejected",
        {"pending-rejected"},
        100ms,
        1ms
    );

    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().find("Rejected"), std::string::npos);
    EXPECT_NE(
        result.error().find("invalid attestation"),
        std::string::npos
    );
}

TEST(DeployClientTest, CommitUncertainKeepsPollingUntilFinalized) {
    LocalHttpServer http;
    std::atomic<int> pending_calls{0};

    http.server.Get(
        R"(/chain/([^/]+)/pending/([^/]+))",
        [&](const httplib::Request& req, httplib::Response& res) {
            const auto call = ++pending_calls;
            const auto state =
                call == 1 ? "CommitUncertain" : "Finalized";

            res.set_content(
                nlohmann::json{
                    {"id", req.matches[2].str()},
                    {"chain_address", req.matches[1].str()},
                    {"state", state},
                    {"error", nullptr}
                }.dump(),
                "application/json"
            );
        }
    );

    http.start();

    DeployClient client(http.base_url());

    const auto result = client.wait_for_finalization(
        "chain-uncertain",
        {"pending-uncertain"},
        200ms,
        1ms
    );

    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_GE(pending_calls.load(), 2);
}

} // namespace
