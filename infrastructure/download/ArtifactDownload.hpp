#pragma once

#include <algorithm>
#include <expected>
#include <string>
#include <vector>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "common/Hash.hpp"
#include "SafeDownload.hpp"

namespace utx::app::infrastructure::download {

// Node /render returns raw, already reconstructed bytes. Independently
// compare size and SHA-256 with the published ArtifactProjector descriptor.
// The descriptor is node-reported state, not a standalone consensus proof.
inline std::expected<std::string, std::string> fetch_artifact(
    const std::string& endpoint, const std::string& chain, const std::string& manifest_md5) {
    constexpr std::size_t max_bytes = 64 * 1024 * 1024;
    if (!safe_chain_id(chain)) return std::unexpected("Invalid artifact chain ID");
    httplib::Client client(endpoint);
    const auto base = "/chain/" + chain;
    auto get_json = [&](const std::string& path) -> std::expected<nlohmann::json, std::string> {
        auto response = client.Get(path);
        if (!response || response->status != 200)
            return std::unexpected("Artifact descriptor unavailable");
        try {
            auto data = nlohmann::json::parse(response->body);
            if (data.contains("data")) return data.at("data");
            return data;
        } catch (const std::exception&) {
            return std::unexpected("Malformed artifact descriptor response");
        }
    };
    const auto config = get_json(base + "/config");
    const auto state = get_json(base + "/state");
    if (!config || !state) return std::unexpected("Artifact config or state unavailable");
    try {
        const auto projectors = config->at("projectors").get<std::vector<std::string>>();
        if (std::ranges::count(projectors, std::string("ArtifactProjector")) != 1)
            return std::unexpected("Chain does not have exactly one ArtifactProjector");
        const auto index = static_cast<std::size_t>(
            std::ranges::find(projectors, std::string("ArtifactProjector")) - projectors.begin());
        const auto& slices = state->at("projector_states");
        if (state->at("_utx_state_model") != "projector-slices-v1" ||
            !slices.is_array() || slices.size() != projectors.size())
            return std::unexpected("Invalid artifact state envelope");
        const auto& published = slices.at(index).at("published");
        if (!published.is_object() || !published.at("total_size").is_number_unsigned())
            return std::unexpected("Artifact has no complete published version");
        const auto size = published.at("total_size").get<std::uint64_t>();
        const auto hash = published.at("sha256").get<std::string>();
        if (size > max_bytes || hash.size() != 64 ||
            published.at("version") != hash)
            return std::unexpected("Invalid or oversized published artifact");

        // Cap data received even if a remote node ignores its response limit.
        std::string bytes;
        auto response = client.Get("/render/" + chain,
            [&](const char* data, std::size_t length) {
                if (length > size - bytes.size()) return false;
                bytes.append(data, length);
                return true;
            });
        if (!response || response->status != 200)
            return std::unexpected("Artifact render unavailable or exceeded its declared size");
        if (bytes.size() != size || common::sha256_hex(bytes) != hash)
            return std::unexpected("Artifact bytes do not match published SHA-256/size");
        // The manifest's existing MD5 is a version consistency check, not
        // an authentication mechanism. Do not silently restore newer bytes.
        if (manifest_md5.empty() || common::md5_hex(bytes) != manifest_md5)
            return std::unexpected("Artifact bytes do not match manifest synchronization hash");
        return bytes;
    } catch (const std::exception&) {
        return std::unexpected("Invalid artifact metadata");
    }
}

} // namespace utx::app::infrastructure::download
