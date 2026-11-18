#pragma once

#include <cstddef>
#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "common/Base64.hpp"
#include "domain/model/AtomicBlock.hpp"

namespace utx::app::infrastructure::deploy {

// Mirrors the explicit HTTP JSON contract of the node's Artifact deployment
// endpoint. The Base64 wrapper is NEVER part of the signed payload.
inline constexpr std::string_view kArtifactChunkPrefix =
    "urn:pi:artifact:chunk:v1:";
inline constexpr std::size_t kMaxArtifactPayloadBytes = 64 * 1024;

[[nodiscard]] inline std::expected<std::string, std::string>
decode_artifact_base64(const std::string& text) {
    if (text.size() > ((kMaxArtifactPayloadBytes + 2) / 3) * 4) {
        return std::unexpected("Artifact chunk exceeds transport size limit");
    }
    const auto decoded = common::base64::decode(text);
    if (decoded.size() > kMaxArtifactPayloadBytes ||
        common::base64::encode(decoded) != text ||
        !decoded.starts_with(kArtifactChunkPrefix)) {
        return std::unexpected("Invalid or noncanonical artifact chunk Base64");
    }
    return decoded;
}

struct PlannedPayload {
    std::string bytes;
    bool artifact_binary = false;
};

// Fail closed on ambiguous plans, unknown payload fields and malformed bytes.
[[nodiscard]] inline std::expected<PlannedPayload, std::string>
decode_planned_payload(const nlohmann::json& plan_tx) {
    try {
        const bool binary = plan_tx.contains("payload_data_b64");
        const bool text = plan_tx.contains("payload_data");
        if (binary == text) {
            return std::unexpected(
                "Plan transaction must contain exactly one payload field");
        }
        if (binary) {
            const auto encoded = plan_tx.at("payload_data_b64").get<std::string>();
            const auto bytes = decode_artifact_base64(encoded);
            if (!bytes) return std::unexpected(bytes.error());
            return PlannedPayload{.bytes = *bytes, .artifact_binary = true};
        }
        const auto value = plan_tx.at("payload_data").get<std::string>();
        if (value.starts_with(kArtifactChunkPrefix)) {
            return std::unexpected("Artifact chunk must use payload_data_b64");
        }
        return PlannedPayload{.bytes = value, .artifact_binary = false};
    } catch (const std::exception& e) {
        return std::unexpected(std::string("Invalid plan transaction: ") + e.what());
    }
}

[[nodiscard]] inline nlohmann::json signed_transaction_for_json(
    const domain::model::SignedTransaction& tx, bool artifact_binary) {
    nlohmann::json j = tx;
    if (artifact_binary) {
        j.erase("data");
        j["data_b64"] = common::base64::encode(tx.data);
    }
    return j;
}

[[nodiscard]] inline std::expected<domain::model::SignedTransaction, std::string>
signed_transaction_from_json(const nlohmann::json& wire) {
    try {
        if (!wire.is_object()) {
            return std::unexpected("Invalid signed transaction object");
        }
        nlohmann::json j = wire;
        if (j.contains("data_b64")) {
            if (j.contains("data")) {
                return std::unexpected("Ambiguous signed transaction payload fields");
            }
            const auto bytes = decode_artifact_base64(
                j.at("data_b64").get<std::string>());
            if (!bytes) return std::unexpected(bytes.error());
            j.erase("data_b64");
            j["data"] = *bytes;  // In-memory only; never dump this raw JSON.
        } else if (j.contains("data") &&
                   j.at("data").is_string() &&
                   j.at("data").get<std::string>().starts_with(kArtifactChunkPrefix)) {
            return std::unexpected("Artifact payload must use data_b64");
        }
        return j.get<domain::model::SignedTransaction>();
    } catch (const std::exception& e) {
        return std::unexpected(std::string("Invalid signed transaction: ") + e.what());
    }
}

} // namespace utx::app::infrastructure::deploy
