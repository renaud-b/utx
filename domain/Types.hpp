#pragma once

#include <expected>
#include <string>
#include <nlohmann/json.hpp>

#include "domain/model/AtomicBlock.hpp"
#include "infrastructure/wallet/WalletHelper.hpp"

using json = nlohmann::json;

namespace utx::app::domain {
    struct DeployRequest {
        std::string chain_id;
        std::string file_path = "index.html";
        std::string content;
        std::string kind = "";
        std::string commit_message = "deploy content";
        bool force_snapshot = true;
        std::string projector = "";
    };

    struct DeployResult {
        bool success;
        std::string plan_id;
    };

    /** Enumeration of target kinds for deployment. */
    enum class TargetKind { Html, Js, Css, Markdown, Go, Graph, Identity, Cpp, Json };
    /** Convert a TargetKind enum value to its string representation.
     * @param k TargetKind enum value.
     * @return String representation of the TargetKind.
     */
    std::string to_string(TargetKind k);
    /** Parse a string into a TargetKind enum value.
     * @param s Input string representing the target kind.
     * @return Optional TargetKind value if parsing is successful; std::nullopt otherwise.
     */
    std::optional<TargetKind> parse_kind(std::string s);
    /** JSON serialization for TargetKind */
    inline void to_json(json& j, const TargetKind& k) {
        j = to_string(k);
    }
    /** JSON deserialization for TargetKind */
    inline void from_json(const json& j, TargetKind& k) {
        auto ok = parse_kind(j.get<std::string>());
        k = ok.value_or(TargetKind::Graph);
    }
    /** Local project configuration (in .utx/config.json) - NOT VERSIONED */
    struct ProjectConfig {
        std::string api_target{"127.0.0.1:8080"};
        std::string wallet_path{};
        std::string deploy_chain{}; // chain that stores the .utx.deploy.json content on-chain

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ProjectConfig, api_target, wallet_path, deploy_chain)
    };
    /** Deployment target (in .utx.deploy.json) - VERSIONED */
    struct DeployTarget {
        std::string path;
        std::string chain;
        TargetKind kind{TargetKind::Graph};
        std::string last_revision_id;
        std::string last_synced_hash;
        std::vector<std::string> genesis_labels{};
    };
    /** JSON serialization for DeployTarget */
    inline void to_json(json& j, const DeployTarget& t) {
        j = json{
            {"path", t.path},
            {"chain", t.chain},
            {"kind", t.kind},
            {"last_revision_id", t.last_revision_id},
            {"last_synced_hash", t.last_synced_hash},
            {"genesis_labels", t.genesis_labels}
        };
    }
    /** JSON deserialization for DeployTarget */
    inline void from_json(const json& j, DeployTarget& t) {
        t.path = j.value("path", "");
        t.chain = j.value("chain", "");
        t.kind = j.contains("kind") ? j.at("kind").get<TargetKind>() : TargetKind::Graph;
        t.last_revision_id = j.value("last_revision_id", "");
        t.last_synced_hash = j.value("last_synced_hash", "");
        t.genesis_labels = j.value("genesis_labels", std::vector<std::string>{});
    }
    /** Deployment configuration structure */
    struct DeployConfig {
        int version{1};
        std::vector<DeployTarget> targets;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(DeployConfig, version, targets)


    };

    // --- Helpers pour les couleurs ---
    namespace color {
        static constexpr const char *reset = "\033[0m";
        static constexpr const char *bold = "\033[1m";
        static constexpr const char *red = "\033[31m";
        static constexpr const char *orange = "\033[38;5;208m";
        static constexpr const char *green = "\033[32m";
        static constexpr const char *grey = "\033[90m";
        static constexpr const char *yellow = "\033[33m";
        static constexpr const char *cyan = "\033[36m";
    }
}
