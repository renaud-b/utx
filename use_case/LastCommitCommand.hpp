#pragma once

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "AbstractCommand.hpp"
#include "domain/graph/Action.hpp"

namespace utx::app::use_case {

class LastCommitCommand final : public AbstractCommand {
public:
    explicit LastCommitCommand(const infrastructure::context::AppContext &ctx)
        : ctx_(ctx) {}

    [[nodiscard]]
    int execute(const std::vector<std::string> &args) override {
        if (args.size() < 2) {
            LOG_THIS_INFO("Usage: utx last-commit --chain_id <id> [--count <N>]");
            return 1;
        }

        std::string chain_id;
        uint64_t count = 20;

        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--help" || args[i] == "-h") {
                LOG_THIS_INFO("Usage: utx last-commit --chain_id <id> [--count <N>]");
                LOG_THIS_INFO("  --chain_id <id> : Chain address to inspect");
                LOG_THIS_INFO("  --count <N>     : Number of latest blocks to scan (default: 20)");
                return 0;
            }
            if (args[i] == "--chain_id" && i + 1 < args.size()) {
                chain_id = args[++i];
                continue;
            }
            if (args[i] == "--count" && i + 1 < args.size()) {
                try {
                    count = std::stoull(args[++i]);
                } catch (...) {
                    LOG_THIS_ERROR("❌ Invalid --count value.");
                    return 1;
                }
            }
        }

        if (chain_id.empty()) {
            LOG_THIS_ERROR("❌ Missing required --chain_id.");
            LOG_THIS_INFO("Usage: utx last-commit --chain_id <id> [--count <N>]");
            return 1;
        }
        if (count == 0) {
            LOG_THIS_ERROR("❌ --count must be >= 1.");
            return 1;
        }

        const auto chain_address = utx::domain::model::Address(chain_id);
        const auto last_block = ctx_.network_client.get_last_block(chain_address);
        if (!last_block) {
            LOG_THIS_ERROR("❌ Chain not found or no blocks available: {}", chain_id);
            return 1;
        }

        const uint64_t end_index = last_block->index;
        const uint64_t start_index = (count > end_index + 1) ? 0 : (end_index + 1 - count);
        const uint64_t expected_block_count = end_index - start_index + 1;

        auto blocks_res = ctx_.network_client.get_chain_segment(chain_address, start_index, expected_block_count);
        if (!blocks_res) {
            LOG_THIS_ERROR("❌ Failed to fetch chain segment: {}", blocks_res.error());
            return 1;
        }

        auto blocks = std::move(*blocks_res);
        std::ranges::sort(blocks, [](const auto &a, const auto &b) {
            return a.index > b.index;
        });

        size_t commit_actions_found = 0;

        for (const auto &block : blocks) {
            const auto commit_actions = extract_commit_actions(block.transaction.payload_data);
            if (commit_actions.empty()) {
                continue;
            }

            for (const auto &action : commit_actions) {
                ++commit_actions_found;
                LOG_THIS_INFO("Block #{} | hash={} | previous={} | at={} ({})",
                              block.index,
                              block.hash.to_string(),
                              block.previous_hash.to_string(),
                              format_block_timestamp(block.timestamp),
                              block.timestamp);
                if (action.payload) {
                    LOG_THIS_INFO("  commit={}", *action.payload);
                } else {
                    LOG_THIS_INFO("  commit=<empty_payload>");
                }
            }
        }

        if (commit_actions_found == 0) {
            LOG_THIS_INFO("No commit actions found in the last {} blocks of chain {}.", expected_block_count, chain_id);
            return 0;
        }

        LOG_THIS_INFO("Found {} commit action(s) in the last {} block(s).", commit_actions_found, expected_block_count);
        return 0;
    }

private:
    enum class TimestampUnit {
        Seconds,
        Milliseconds
    };

    [[nodiscard]]
    static uint64_t abs_diff(const uint64_t a, const uint64_t b) {
        return (a >= b) ? (a - b) : (b - a);
    }

    [[nodiscard]]
    static TimestampUnit infer_timestamp_unit(const uint64_t raw_timestamp) {
        const uint64_t now_seconds = static_cast<uint64_t>(std::time(nullptr));
        const uint64_t as_seconds = raw_timestamp;
        const uint64_t as_milliseconds = raw_timestamp / 1000ULL;

        if (abs_diff(as_milliseconds, now_seconds) < abs_diff(as_seconds, now_seconds)) {
            return TimestampUnit::Milliseconds;
        }
        return TimestampUnit::Seconds;
    }

    [[nodiscard]]
    static std::string format_block_timestamp(const uint64_t raw_timestamp) {
        const auto inferred_unit = infer_timestamp_unit(raw_timestamp);
        const uint64_t normalized_seconds = (inferred_unit == TimestampUnit::Milliseconds)
                                                ? (raw_timestamp / 1000ULL)
                                                : raw_timestamp;
        std::time_t ts = static_cast<std::time_t>(normalized_seconds);
        std::tm tm_utc{};
#if defined(_WIN32)
        if (gmtime_s(&tm_utc, &ts) != 0) {
            return "<invalid_timestamp>";
        }
#else
        if (gmtime_r(&ts, &tm_utc) == nullptr) {
            return "<invalid_timestamp>";
        }
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm_utc, "%Y-%m-%d %H:%M:%S UTC");
        if (inferred_unit == TimestampUnit::Milliseconds) {
            oss << " [raw-ms]";
        }
        return oss.str();
    }

    [[nodiscard]]
    static std::vector<utx::domain::graph::Action> extract_commit_actions(const std::string &payload) {
        std::vector<utx::domain::graph::Action> commits;

        utx::domain::graph::Action action;
        try {
            action = utx::domain::graph::Action::decode_blockchain_action(payload);
        } catch (...) {
            return commits;
        }

        if (action.type == utx::domain::graph::ActionType::COMMIT_TAG) {
            commits.push_back(action);
            return commits;
        }

        if (action.type != utx::domain::graph::ActionType::GROUP || !action.payload) {
            return commits;
        }

        try {
            const auto grouped = nlohmann::json::parse(*action.payload);
            if (!grouped.is_array()) {
                return commits;
            }
            for (const auto &entry : grouped) {
                if (!entry.is_string()) {
                    continue;
                }
                auto sub = utx::domain::graph::Action::decode_action(entry.get<std::string>());
                if (sub.type == utx::domain::graph::ActionType::COMMIT_TAG) {
                    commits.push_back(std::move(sub));
                }
            }
        } catch (...) {
            return commits;
        }

        return commits;
    }

    infrastructure::context::AppContext ctx_;
};

} // namespace utx::app::use_case
