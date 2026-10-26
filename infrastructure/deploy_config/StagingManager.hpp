#pragma once

#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace utx::app::infrastructure::deploy {

// Local-only deployment selection. The manifest continues to describe ALL tracked
// targets; this file lists only the targets explicitly selected for the next deploy.
class StagingManager {
public:
    static constexpr const char* kStagingFile = "staging.json";

    [[nodiscard]]
    static std::expected<std::vector<std::string>, std::string>
    load(const std::filesystem::path& root) {
        const auto path = root / ".utx" / kStagingFile;
        try {
            if (!std::filesystem::exists(path)) {
                return std::vector<std::string>{};
            }

            std::ifstream in(path);
            if (!in) {
                return std::unexpected("Cannot read staging file: " + path.string());
            }

            const auto data = nlohmann::json::parse(in);
            if (!data.is_object() ||
                data.value("version", 0) != 1 ||
                !data.contains("paths") ||
                !data.at("paths").is_array()) {
                return std::unexpected("Invalid staging format: " + path.string());
            }

            std::vector<std::string> paths;
            for (const auto& entry : data.at("paths")) {
                if (!entry.is_string() || entry.get<std::string>().empty()) {
                    return std::unexpected("Invalid staged path in: " + path.string());
                }
                const auto value = entry.get<std::string>();
                if (std::find(paths.begin(), paths.end(), value) == paths.end()) {
                    paths.push_back(value);
                }
            }
            return paths;
        } catch (const std::exception& e) {
            return std::unexpected(
                "Failed to load staging file " + path.string() + ": " + e.what()
            );
        }
    }

    [[nodiscard]]
    static std::expected<void, std::string>
    save(const std::filesystem::path& root, const std::vector<std::string>& paths) {
        const auto dir = root / ".utx";
        const auto path = dir / kStagingFile;
        const auto tmp = dir / "staging.json.tmp";
        try {
            std::filesystem::create_directories(dir);
            {
                std::ofstream out(tmp, std::ios::trunc);
                if (!out) {
                    return std::unexpected("Cannot write staging file: " + tmp.string());
                }
                out << nlohmann::json{
                    {"version", 1}, {"paths", paths}
                }.dump(4);
                out.flush();
                if (!out) {
                    return std::unexpected("Failed writing staging file: " + tmp.string());
                }
            }
            // POSIX atomic replacement; never delete the existing selection first.
            std::error_code ec;
            std::filesystem::rename(tmp, path, ec);
            if (ec) {
                return std::unexpected("Failed replacing staging file: " + ec.message());
            }
            return {};
        } catch (const std::exception& e) {
            return std::unexpected(std::string("Failed to save staging: ") + e.what());
        }
    }
};

} // namespace utx::app::infrastructure::deploy
