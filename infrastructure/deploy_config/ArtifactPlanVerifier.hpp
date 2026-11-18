#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "ArtifactTransport.hpp"
#include "common/Hash.hpp"

namespace utx::app::infrastructure::deploy {

// Confirm that an untrusted node's proposed bytes match the user's local
// content before the wallet authorizes any transaction. Permits a suffix-only
// plan when an interrupted publication is resumed.
[[nodiscard]] inline std::expected<void, std::string>
verify_artifact_plan(
    const nlohmann::json& plan,
    const std::string& local_content,
    const std::string& expected_mime) {
    constexpr std::size_t kChunkBytes = 24 * 1024;
    constexpr std::size_t kMaxFileBytes = 8 * 1024 * 1024;
    if (local_content.size() > kMaxFileBytes) {
        return std::unexpected("Artifact exceeds node prepare limit");
    }
    try {
        if (plan.at("strategy").get<std::string>() != "artifact") {
            return std::unexpected("Unexpected artifact deploy strategy");
        }
        const auto version = common::sha256_hex(local_content);
        if (plan.contains("content_hash") &&
            plan.at("content_hash").get<std::string>() != version) {
            return std::unexpected("Artifact plan content hash mismatch");
        }
        const auto& txs = plan.at("transactions");
        if (!txs.is_array() || txs.empty()) {
            return std::unexpected("Empty artifact transaction plan");
        }
        const auto expected_count = std::max<std::size_t>(
            1, (local_content.size() + kChunkBytes - 1) / kChunkBytes);

        std::optional<std::size_t> last_index;
        bool has_genesis = false;
        for (std::size_t tx_index = 0; tx_index < txs.size(); ++tx_index) {
            const auto payload = decode_planned_payload(txs.at(tx_index));
            if (!payload) return std::unexpected(payload.error());
            if (!payload->artifact_binary) {
                if (tx_index != 0 || has_genesis) {
                    return std::unexpected("Unexpected non-artifact transaction");
                }
                // A genesis is permitted only as the first transaction, with
                // exactly the initial Owner + Artifact projector composition.
                const auto decoded = common::base64::decode(payload->bytes);
                if (common::base64::encode(decoded) != payload->bytes) {
                    return std::unexpected("Invalid artifact genesis Base64");
                }
                const auto config = nlohmann::json::from_msgpack(
                    decoded.begin(), decoded.end());
                const auto projectors =
                    config.at("projectors").get<std::vector<std::string>>();
                if (projectors != std::vector<std::string>{
                        "OwnerProjector", "ArtifactProjector"}) {
                    return std::unexpected("Incorrect Artifact genesis projectors");
                }
                has_genesis = true;
                continue;
            }

            const auto bytes = std::string_view(payload->bytes).substr(
                kArtifactChunkPrefix.size());
            const auto chunk = nlohmann::json::from_msgpack(
                bytes.begin(), bytes.end());
            if (!chunk.is_object() ||
                chunk.at("version").get<std::string>() != version ||
                !chunk.at("index").is_number_unsigned() ||
                !chunk.at("bytes").is_binary()) {
                return std::unexpected("Invalid Artifact chunk descriptor");
            }
            const auto index = chunk.at("index").get<std::size_t>();
            if (index >= expected_count ||
                (last_index && index != *last_index + 1) ||
                (has_genesis && !last_index && index != 0)) {
                return std::unexpected("Artifact chunk sequence does not match local file");
            }
            const auto offset = index * kChunkBytes;
            const auto len = std::min(kChunkBytes, local_content.size() - offset);
            const auto& binary = chunk.at("bytes").get_binary();
            if (binary.size() != len ||
                !std::equal(binary.begin(), binary.end(),
                    local_content.begin() + static_cast<std::ptrdiff_t>(offset),
                    [](unsigned char a, char b) {
                        return a == static_cast<unsigned char>(b);
                    })) {
                return std::unexpected("Artifact chunk bytes do not match local file");
            }
            if (index == 0) {
                if (!chunk.contains("metadata")) {
                    return std::unexpected("Missing Artifact first-chunk metadata");
                }
                const auto& m = chunk.at("metadata");
                if (m.at("sha256").get<std::string>() != version ||
                    m.at("mime").get<std::string>() != expected_mime ||
                    m.at("total_size").get<std::size_t>() != local_content.size() ||
                    m.at("chunk_count").get<std::size_t>() != expected_count) {
                    return std::unexpected("Artifact metadata does not match local file");
                }
            } else if (chunk.contains("metadata")) {
                return std::unexpected("Unexpected Artifact continuation metadata");
            }
            last_index = index;
        }
        if (!last_index || *last_index != expected_count - 1) {
            return std::unexpected("Artifact plan does not contain final chunk");
        }
        return {};
    } catch (const std::exception& e) {
        return std::unexpected(std::string("Malformed Artifact plan: ") + e.what());
    }
}

} // namespace utx::app::infrastructure::deploy
