#pragma once
#include <algorithm>
#include <cstdint>
#include <expected>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include "SafeDownload.hpp"

namespace utx::app::infrastructure::download {
struct OwnerObservation {
    std::string chain;
    std::uint64_t height{};
    std::string head_hash;
    std::vector<std::string> owners;
    bool operator==(const OwnerObservation&) const = default;
};

inline std::expected<nlohmann::json, std::string> read_public_json(
    const std::string& endpoint, const std::string& path) {
    httplib::Client client(endpoint);
    client.set_connection_timeout(5, 0);
    client.set_read_timeout(10, 0);
    auto response = client.Get(path);
    if (!response || response->status != 200)
        return std::unexpected("Public chain data unavailable");
    try {
        auto data = nlohmann::json::parse(response->body);
        if (data.contains("data")) return data.at("data");
        return data;
    } catch (const std::exception&) {
        return std::unexpected("Invalid public chain response");
    }
}

inline std::expected<OwnerObservation, std::string> read_owners(
    const std::string& endpoint, const std::string& chain) {
    if (!safe_chain_id(chain)) return std::unexpected("Invalid manifest chain ID");
    const auto response = read_public_json(endpoint, "/chain/" + chain + "/owners");
    if (!response) return std::unexpected(response.error());
    try {
        if (response->at("chain_id") != chain || !response->at("height").is_number_unsigned())
            return std::unexpected("Owner response does not identify requested chain");
        OwnerObservation observation{chain, response->at("height").get<std::uint64_t>(),
            response->at("head_hash").get<std::string>(),
            response->at("owners").get<std::vector<std::string>>()};
        const auto& hash = observation.head_hash;
        if (hash.size() != 64 || !std::ranges::all_of(hash, [](unsigned char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            }) || observation.owners.empty() || observation.owners.size() > 128)
            return std::unexpected("Invalid owner observation");
        std::set<std::string> unique;
        for (const auto& owner : observation.owners)
            if (!safe_chain_id(owner) || !unique.insert(owner).second)
                return std::unexpected("Invalid or duplicate owner address");
        return observation;
    } catch (const std::exception&) {
        return std::unexpected("Malformed owner observation");
    }
}

// Preserve valid UTF-8, neutralize control codes and directional overrides.
inline std::string terminal_text(std::string_view input, std::size_t limit) {
    std::string output;
    std::size_t printed = 0;
    while (!input.empty() && printed < limit) {
        const auto first = static_cast<unsigned char>(input.front());
        const std::size_t n = first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2 :
            first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        if (!n || input.size() < n) {
            output += '?'; input.remove_prefix(1); ++printed; continue;
        }
        std::uint32_t code = n == 1 ? first : first & (0x7f >> n);
        bool valid = true;
        for (std::size_t i = 1; i < n; ++i) {
            const auto c = static_cast<unsigned char>(input[i]);
            if ((c & 0xc0) != 0x80) { valid = false; break; }
            code = (code << 6) | (c & 0x3f);
        }
        if (!valid || (n == 2 && code < 0x80) || (n == 3 && code < 0x800) ||
            (n == 4 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff)) {
            output += '?'; input.remove_prefix(1); ++printed; continue;
        }
        const bool control = code < 0x20 || (code >= 0x7f && code <= 0x9f) ||
            code == 0x061c || code == 0x200e || code == 0x200f ||
            (code >= 0x2028 && code <= 0x202e) || (code >= 0x2066 && code <= 0x2069);
        if (control) output += ' ';
        else output.append(input.substr(0, n));
        input.remove_prefix(n);
        ++printed;
    }
    if (!input.empty()) output += "...";
    return output;
}

struct OwnerProfile { std::string pseudo, description, picture; };

inline std::optional<OwnerProfile> read_profile(
    const std::string& endpoint, const std::string& owner) {
    if (!safe_chain_id(owner)) return std::nullopt;
    auto response = read_public_json(endpoint, "/graph/" + owner);
    if (!response) return std::nullopt;
    try {
        if (response->at("graph_id") != owner) return std::nullopt;
        const auto& root = response->at("root");
        auto field = [&](const char* key) {
            return root.contains(key) && root.at(key).is_string() ?
                root.at(key).get<std::string>() : std::string{};
        };
        return OwnerProfile{field("user.pseudo"), field("user.description"), field("user.picture")};
    } catch (const std::exception&) { return std::nullopt; }
}

inline void print_project_summary(std::ostream& out, const OwnerObservation& observation,
                                  const domain::DeployConfig& config) {
    out << "\nManifest: " << observation.chain << "\nObserved head: #" << observation.height
        << " " << observation.head_hash << "\nProject contents: " << config.targets.size() << " file(s)\n";
    std::map<std::string, std::size_t> counts;
    for (const auto& target : config.targets) ++counts[domain::to_string(target.kind)];
    for (const auto& [kind, count] : counts) out << "  " << kind << ": " << count << "\n";
    out << "\nCurrent manifest owner(s), as reported by the selected node:\n";
}

inline bool confirm_download(std::istream& in, std::ostream& out) {
    out << "\nDownload this project? [y/N] " << std::flush;
    std::string answer;
    if (!std::getline(in, answer)) return false;
    return answer == "y" || answer == "Y" || answer == "yes" || answer == "YES" || answer == "Yes";
}
} // namespace utx::app::infrastructure::download
