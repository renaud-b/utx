#pragma once

#include <expected>
#include <string>
#include <optional>
#include "../graph/Graph.hpp"

namespace utx::domain::deploy {
    struct DeployPlanInput {
        std::string chain_id;
        std::string file_path;
        std::string sender_address;
        std::string kind;
        std::string projector;
        std::string content;           // raw HTML/JS/etc
        std::string commit_message;           // The commit message associated with this deploy (for traceability)
        std::optional<graph::Graph> remote_graph;
        bool force_snapshot;
        bool force_group_actions;
        bool is_new_chain; // true if no remote graph exists
        size_t group_action_size;
    };

    struct PlannedTransaction {
        std::string payload_data;  // urn:pi:graph:...
    };

    struct DeployPlan {
        std::string plan_id;
        std::string strategy; // "snapshot" or "incremental"
        std::vector<PlannedTransaction> transactions;
        std::string content_hash;
        std::string summary;

        std::string to_string() const {
            std::stringstream ss;
            ss << "DeployPlan{plan_id=" << plan_id
               << ", strategy=" << strategy
               << ", content_hash=" << content_hash
               << ", summary=" << summary
               << ", transactions=[";
            for (const auto& tx : transactions) {
                const auto tx_payload_substr = tx.payload_data.size() > 30 ? tx.payload_data.substr(0, 30) + "..." : tx.payload_data;
                ss << "{payload_data=" << tx_payload_substr << "}, ";
            }
            ss << "]}";
            return ss.str();
        }
    };

    class DeployPlanner {
    public:
        static std::expected<DeployPlan, std::string>
        plan(const DeployPlanInput& input);
    };
}