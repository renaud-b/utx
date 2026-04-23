#pragma once

#include "AbstractCommand.hpp"
#include "domain/graph/GraphElement.hpp"
#include "infrastructure/chain/TxManager.hpp"

namespace utx::app::use_case {
    /** cmd_graph : Manage Utopixia graphs.
     * utx graph root <chain_id>
     * utx graph show <chain_id> [--depth <n>]
     * utx graph update <chain_id> <element_id> <property> <value>
     */
    class GraphCommand final : public AbstractCommand {
    public:
        explicit GraphCommand(
            const infrastructure::context::AppContext &ctx
        ) : ctx_(ctx) {
        }

        int execute(const std::vector<std::string> &args) override {
            if (args.size() < 3 || args[2] == "--help" || args[2] == "-h") {
                LOG_THIS_INFO(
                    "Usage:\n  utx graph root <chain_id>\n  utx graph show <chain_id> [--depth <n>]\n  utx graph update <chain_id> <element_id> <property> <value>");
                return 1;
            }
            const std::string subcmd = args[2];
            if (subcmd == "root") {
                if (args.size() < 4) {
                    LOG_THIS_INFO("Usage: utx graph root <chain_id>");
                    return 1;
                }
                const std::string chain_id = args[3];
                auto graph = ctx_.network_client.fetch_graph_state(chain_id);
                if (!graph) {
                    LOG_THIS_ERROR("❌ Could not fetch graph for chain ID: {}", chain_id);
                    return 1;
                }
                auto root_element = graph->root();
                if (!root_element) {
                    LOG_THIS_ERROR("❌ Graph has no root element.");
                    return 1;
                }
                LOG_THIS_INFO("🌳 Root element of graph {}: ID={}, Name={}, Path={}, Properties=[{}]",
                              chain_id,
                              root_element->id(),
                              root_element->name(),
                              root_element->path(),
                              fmt::join(root_element->properties() | std::views::keys, ", "));
                return 0;
            }
            if (subcmd == "show") {
                if (args.size() < 4) {
                    LOG_THIS_INFO("Usage: utx graph show <chain_id> [--depth <n>]");
                    return 1;
                }
                const std::string chain_id = args[3];
                auto graph = ctx_.network_client.fetch_graph_state(chain_id);
                if (!graph) {
                    LOG_THIS_ERROR("❌ Could not fetch graph for chain ID: {}", chain_id);
                    return 1;
                }
                // Start to print the graph.
                print_node(graph->root());
                return 0;
            }
            if (subcmd == "update") {
                if (!ctx_.wallet) {
                    LOG_THIS_ERROR("{}❌ No wallet configured.{}", utx::app::domain::color::red,
                                   utx::app::domain::color::reset);
                    return 1;
                }

                if (args.size() < 7) {
                    LOG_THIS_INFO("Usage: utx graph update <chain_id> <element_id> <property> <value>");
                    return 1;
                }

                const std::string chain_id = args[3];
                const std::string element_id = args[4];
                const std::string property = args[5];
                const std::string value = args[6];

                auto deploy_client = ctx_.deploy_client();

                auto graph_state = ctx_.network_client.fetch_graph_state(chain_id);
                if (!graph_state) {
                    LOG_THIS_ERROR("❌ Could not fetch graph for chain ID: {}", chain_id);
                    return 1;
                }
                auto target_element = graph_state->find_element(element_id);
                if (!target_element) {
                    LOG_THIS_ERROR("❌ Element with ID {} not found in graph {}.", element_id, chain_id);
                    return 1;
                }

                target_element->set_property(property, value);
                nlohmann::json update_content = graph_state->to_json();

                domain::DeployRequest req;
                req.chain_id = chain_id;
                req.file_path = "graph_update";
                req.kind = "graph";
                req.content = update_content.dump();
                req.commit_message = "graph update";
                req.force_snapshot = true; // Force snapshot to update the whole graph

                const auto my_address = utx::domain::model::Address(ctx_.wallet->address);

                // 🔥 PREPARE
                auto plan_res = deploy_client.prepare(req, my_address.to_string());

                if (!plan_res) {
                    LOG_THIS_ERROR("❌ Prepare failed: {}", plan_res.error());
                    return 1;
                }

                const auto &plan = *plan_res;

                if (!plan.contains("transactions") || !plan["transactions"].is_array()) {
                    LOG_THIS_ERROR("❌ Invalid plan");
                    return 1;
                }

                const auto &txs = plan["transactions"];

                if (txs.empty()) {
                    LOG_THIS_WARN("⚠️ Nothing to update.");
                    return 0;
                }

                // 🔥 SIGN
                nlohmann::json signed_txs = nlohmann::json::array();

                for (const auto &tx: txs) {
                    const std::string payload = tx["payload_data"].get<std::string>();

                    auto signed_res =
                            deploy_client.build_signed_tx(payload, *ctx_.wallet);

                    if (!signed_res) {
                        LOG_THIS_ERROR("❌ Signing failed: {}", signed_res.error());
                        return 1;
                    }

                    signed_txs.push_back(signed_res.value());
                }

                // 🔥 SUBMIT
                auto submit_res =
                        deploy_client.submit(plan["plan_id"], chain_id, signed_txs);

                if (!submit_res) {
                    LOG_THIS_ERROR("❌ Submit failed: {}", submit_res.error());
                    return 1;
                }

                LOG_THIS_INFO("✅ Updated element {} on chain {}: {} = {}",
                              element_id, chain_id, property, value);

                return 0;
            }

            return 1;
        }

    private:
        static void print_node(const std::shared_ptr<utx::domain::graph::GraphElement> &element) {
            LOG_THIS_INFO("ID: {}, Name: {}, Path: {}, Properties: [{}]",
                          element->id(),
                          element->name(),
                          element->path(),
                          fmt::join(element->properties() | std::views::keys, ", "));
            for (auto child: element->children()) {
                print_node(child);
            }
        }

        infrastructure::context::AppContext ctx_;
    };
}
