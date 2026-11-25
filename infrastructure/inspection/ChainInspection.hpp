#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <expected>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "infrastructure/download/SafeDownload.hpp"
#include "TerminalAvatar.hpp"

namespace utx::app::infrastructure::inspection {

struct OwnerProfile {
    std::string address;
    std::string pseudo;
    std::string description;
    std::string picture;
    bool available{false};
};

struct ChainInfo {
    std::string chain_id;
    std::string endpoint;
    std::optional<std::uint64_t> height;
    std::string head_hash;
    std::vector<std::string> projectors;
    std::vector<std::string> labels;
    std::vector<OwnerProfile> owners;
};

inline bool valid_owner_address(std::string_view address) {
    if (address.size() != 42 || !address.starts_with("0x")) return false;
    for (std::size_t i = 2; i < address.size(); ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(address[i]))) return false;
    }
    return true;
}

inline std::string safe_display_text(std::string_view text, std::size_t limit = 512) {
    std::string output;
    output.reserve(std::min(text.size(), limit));
    for (const unsigned char c : text) {
        if (output.size() >= limit) break;
        if (c == '\r' || c == '\n' || c == '\t') {
            output.push_back(' ');
        } else if (c >= 32 && c != 127) {
            output.push_back(static_cast<char>(c));
        }
    }
    return output;
}

inline std::string json_text(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_string()) return {};
    return object.at(key).get<std::string>();
}

inline std::expected<ChainInfo, std::string> parse_chain_config(
    const nlohmann::json& config, std::string chain_id, std::string endpoint) {
    if (!config.is_object() ||
        !config.contains("projectors") || !config.at("projectors").is_array()) {
        return std::unexpected("Missing projectors in chain configuration");
    }
    ChainInfo info{.chain_id = std::move(chain_id), .endpoint = std::move(endpoint)};
    try {
        info.projectors = config.at("projectors").get<std::vector<std::string>>();
        if (std::ranges::count(info.projectors, std::string("OwnerProjector")) != 1)
            return std::unexpected("Chain configuration requires exactly one OwnerProjector");
        if (config.contains("labels"))
            info.labels = config.at("labels").get<std::vector<std::string>>();
    } catch (const std::exception&) {
        return std::unexpected("Invalid chain configuration");
    }
    return info;
}

// The node's public /owners view resolves current canonical OwnerProjector state,
// including transfers. ChainConfig has no owners field. Never fall back to
// genesis/config/profile metadata if this view is unavailable.
inline std::expected<void, std::string> populate_canonical_owners(
    ChainInfo& info, const nlohmann::json& view) {
    if (!view.is_object() || json_text(view, "chain_id") != info.chain_id ||
        !view.contains("owners") || !view.at("owners").is_array() ||
        !view.contains("height") || !view.at("height").is_number_unsigned()) {
        return std::unexpected("Invalid canonical owner view");
    }
    const auto last_index = view.at("height").get<std::uint64_t>();
    if (last_index == std::numeric_limits<std::uint64_t>::max())
        return std::unexpected("Invalid chain height");

    std::vector<OwnerProfile> owners;
    for (const auto& address : view.at("owners")) {
        if (!address.is_string() || !valid_owner_address(address.get<std::string>()))
            return std::unexpected("Invalid canonical owner address");
        owners.push_back(OwnerProfile{.address = address.get<std::string>()});
    }
    if (owners.empty()) return std::unexpected("Canonical owner set is empty");
    info.owners = std::move(owners);
    // The V1 /owners payload calls the last block index 'height'.
    info.height = last_index + 1;
    info.head_hash = json_text(view, "head_hash");
    return {};
}

// Identity metadata is optional presentation data. Never use profile properties
// as evidence of chain ownership; only canonical OwnerProjector state does.
inline void populate_profile(OwnerProfile& owner, const nlohmann::json& state) {
    if (!state.is_object() || json_text(state, "_utx_state_model") != "projector-slices-v1" ||
        !state.contains("projector_states") || !state.at("projector_states").is_array()) return;

    const nlohmann::json* root = nullptr;
    for (const auto& slice : state.at("projector_states")) {
        if (!slice.is_object() || !slice.contains("graph_data")) continue;
        const auto& graph = slice.at("graph_data");
        if (!graph.is_object() || !graph.contains("root") || !graph.at("root").is_object()) return;
        if (root) return; // Never choose arbitrarily between ambiguous slices.
        root = &graph.at("root");
    }
    if (!root) return;
    owner.available = true;
    owner.pseudo = json_text(*root, "user.pseudo");
    owner.description = json_text(*root, "user.description");
    owner.picture = json_text(*root, "user.picture");
}

inline std::expected<nlohmann::json, std::string> fetch_json(
    httplib::Client& client, const std::string& path) {
    const auto response = client.Get(path);
    if (!response) return std::unexpected("Node unavailable for " + path);
    if (response->status != 200)
        return std::unexpected(std::format("HTTP {} for {}", response->status, path));
    try {
        auto body = nlohmann::json::parse(response->body);
        if (!body.is_object()) return std::unexpected("Invalid JSON response for " + path);
        if (body.contains("data")) return body.at("data");
        return body;
    } catch (const std::exception&) {
        return std::unexpected("Malformed JSON response for " + path);
    }
}

inline std::expected<ChainInfo, std::string> fetch_chain_info(
    const std::string& endpoint, const std::string& chain_id) {
    if (!download::safe_chain_id(chain_id))
        return std::unexpected("Invalid chain ID");

    httplib::Client client(endpoint);
    client.set_connection_timeout(4);
    client.set_read_timeout(5);

    const auto config = fetch_json(client, "/chain/" + chain_id + "/config");
    if (!config) return std::unexpected("Cannot inspect chain: " + config.error());

    auto parsed = parse_chain_config(*config, chain_id, endpoint);
    if (!parsed) return parsed;

    const auto owners = fetch_json(client, "/chain/" + chain_id + "/owners");
    if (!owners) return std::unexpected("Cannot read current chain owners: " + owners.error());
    if (const auto loaded = populate_canonical_owners(*parsed, *owners); !loaded)
        return std::unexpected(loaded.error());

    for (auto& owner : parsed->owners) {
        const auto state = fetch_json(client, "/chain/" + owner.address + "/state");
        if (state) populate_profile(owner, *state);
    }
    return parsed;
}

inline void print_chain_info(const ChainInfo& info, std::ostream& out = std::cout,
                             bool show_avatar = true) {
    out << "\n  Chain      : " << safe_display_text(info.chain_id) << '\n';
    out << "  API        : " << safe_display_text(info.endpoint) << '\n';
    out << "  Height     : "
        << (info.height ? std::to_string(*info.height) : "unavailable") << '\n';
    if (!info.head_hash.empty())
        out << "  Head hash  : " << safe_display_text(info.head_hash, 128) << '\n';
    out << "  Projectors :";
    if (info.projectors.empty()) out << " (none)";
    for (const auto& projector : info.projectors) out << " " << safe_display_text(projector, 128);
    out << '\n';
    if (!info.labels.empty()) {
        out << "  Labels     :";
        for (const auto& label : info.labels) out << " " << safe_display_text(label, 128);
        out << '\n';
    }

    out << "  Owners (" << info.owners.size() << "):\n";
    for (const auto& owner : info.owners) {
        out << "    Address     : " << owner.address << '\n';
        if (!owner.available) {
            out << "    Profile     : unavailable\n";
            continue;
        }
        out << "    Pseudo      : " << (owner.pseudo.empty() ? "(not set)" : safe_display_text(owner.pseudo, 128)) << '\n';
        out << "    Description : " << (owner.description.empty() ? "(not set)" : safe_display_text(owner.description)) << '\n';
        if (!owner.picture.empty()) {
            out << "    Picture     : " << safe_display_text(owner.picture, 256) << '\n';
            out.flush();
            if (show_avatar && &out == &std::cout) preview_avatar(owner.picture);
        }
        out << '\n';
    }
    out << "  Note: owners reflect the node-reported canonical OwnerProjector state;\n"
           "        this CLI does not independently verify chain proofs.\n\n";
}

inline bool confirm_download(std::istream& in, std::ostream& out) {
    out << "  Download these files into the current directory? [y/N] " << std::flush;
    std::string answer;
    if (!std::getline(in, answer)) {
        out << '\n';
        return false;
    }
    out << '\n';
    const auto start = answer.find_first_not_of(" \t");
    if (start == std::string::npos) return false;
    const auto end = answer.find_last_not_of(" \t");
    answer = answer.substr(start, end - start + 1);
    for (auto& c : answer) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return answer == "y" || answer == "yes";
}

} // namespace utx::app::infrastructure::inspection
