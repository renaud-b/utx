#pragma once

#include <expected>
#include <format>
#include <string>
#include <optional>
#include <ranges>
#include <vector>
#include <httplib.h>

#include "domain/Types.hpp"
#include "common/Hash.hpp"
#include "domain/model/AtomicBlock.hpp"
#include "domain/model/ChainConfig.hpp"
#include "domain/model/NodeInfo.hpp"
#include "domain/graph/Graph.hpp"
#include "domain/graph/Action.hpp"
#include "domain/graph/GraphElement.hpp"
#include "infrastructure/wallet/WalletHelper.hpp"
#include "infrastructure/crypto/OpenSSLCryptoService.hpp"
#include "infrastructure/network/Serialization.hpp"

namespace utx::app::infrastructure::chain {
    using namespace utx::domain::model;
    using namespace utx::domain::graph;
    using namespace utx::app::domain;

    struct NetworkClient {
        std::string target;
        [[nodiscard]]
        std::optional<Graph> get_graph(const Address &addr) const {
            httplib::Client cli(target);
            if (auto res = cli.Get("/graph/" + addr.to_string())) {
                if (res->status == 200) {
                    return Graph::from_json(res->body);
                }
                if (res->status != 404) {
                    LOG_THIS_ERROR("{}❌ Failed to fetch last block (status: {}).{}", color::red, res->status,
                                   color::reset);
                }
                // Check the error
                const auto err = res.error();
                if (err != httplib::Error::Success) {
                    LOG_THIS_ERROR("{}❌ Node error: {}.", color::red, httplib::to_string(err));
                }
            }
            return std::nullopt;
        }

        [[nodiscard]]
        std::optional<AtomicBlock> get_last_block(const Address &addr) const {
            httplib::Client cli(target);
            if (auto res = cli.Get("/chain/" + addr.to_string() + "/last")) {
                if (res->status == 200) return json::parse(res->body).get<AtomicBlock>();
                if (res->status != 404) {
                    LOG_THIS_ERROR("{}❌ Failed to fetch last block (status: {}).{}", color::red, res->status,
                                   color::reset);
                }
                // Check the error
                const auto err = res.error();
                if (err != httplib::Error::Success) {
                    LOG_THIS_ERROR("{}❌ Node error: {}.", color::red, httplib::to_string(err));
                }
            }
            return std::nullopt;
        }

        [[nodiscard]]
        std::expected<std::vector<AtomicBlock>, std::string> get_chain_segment(
            const Address &addr,
            const uint64_t start_index,
            const uint64_t block_count) const {
            if (block_count == 0) {
                return std::vector<AtomicBlock>{};
            }

            constexpr uint64_t kPageSize = 100;
            uint64_t cursor = start_index;
            const uint64_t end_exclusive = start_index + block_count;
            std::vector<AtomicBlock> collected;
            collected.reserve(static_cast<size_t>(block_count));

            httplib::Client cli(target);
            while (cursor < end_exclusive) {
                const auto path = std::format("/chain/{}/sync/{}", addr.to_string(), cursor);
                auto res = cli.Get(path);
                if (!res) {
                    return std::unexpected("Failed to connect to node");
                }
                if (res->status != 200) {
                    return std::unexpected(std::format("Failed to fetch chain segment (status={})", res->status));
                }

                std::vector<AtomicBlock> page;
                try {
                    page = json::parse(res->body).get<std::vector<AtomicBlock>>();
                } catch (const std::exception &e) {
                    return std::unexpected(std::format("Invalid chain segment payload: {}", e.what()));
                }
                if (page.empty()) {
                    break;
                }

                std::ranges::sort(page, [](const AtomicBlock &a, const AtomicBlock &b) {
                    return a.index < b.index;
                });

                for (const auto &block : page) {
                    if (block.index >= end_exclusive) {
                        break;
                    }
                    if (block.index < cursor) {
                        continue;
                    }
                    collected.push_back(block);
                    cursor = block.index + 1;
                }

                if (page.size() < kPageSize) {
                    break;
                }
            }

            return collected;
        }

        [[nodiscard]]
        std::expected<Graph, std::string> fetch_graph_state(const std::string &graph_uuid) const {
            httplib::Client cli(target);
            if (auto res = cli.Get("/graph/" + graph_uuid)) {
                if (res->status == 200) {
                    try {
                        return Graph::from_json(res->body);
                    } catch (...) {
                        return std::unexpected("Failed to parse graph state.");
                    }
                }
                return std::unexpected("Graph not found or node unreachable.");
            }
            return std::unexpected("Failed to connect to node.");
        }

        [[nodiscard]]
        std::expected<std::vector<NodeInfo>, std::string> fetch_cluster_status() const {
            httplib::Client cli(target);
            if (auto res = cli.Get("/network/peers")) {
                if (res->status == 200) {
                    try {
                        return json::parse(res->body).get<std::vector<NodeInfo> >();
                    } catch (...) {
                        return std::unexpected("Failed to parse cluster status.");
                    }
                }
                return std::unexpected(
                    "Failed to fetch cluster status (status code: " + std::to_string(res->status) + ").");
            }
            return std::unexpected("Failed to connect to node.");
        }

    };
}
