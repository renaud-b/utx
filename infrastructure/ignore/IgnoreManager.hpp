#pragma once

#include <filesystem>
#include <algorithm>
#include <vector>
#include <string>
#include <regex>
#include <ranges>

#include "../deploy_config/DeployConfig.hpp"

namespace utx::app::infrastructure::ignore {
    static constexpr const char *kIgnoreFile = ".utxignore";

    namespace fs = std::filesystem;

    static std::string normalize_rel_path(std::string s) {
        // ensure forward slashes + no leading "./"
        std::ranges::replace(s, '\\', '/');
        while (s.starts_with("./")) s.erase(0, 2);
        if (s == ".") s.clear();
        return s;
    }


    struct IgnoreRule {
        bool negated{false};
        bool dir_only{false};
        std::regex rx;

        bool matches(const std::string &rel, bool is_dir) const {
            if (dir_only && !is_dir) return false;
            return std::regex_match(rel, rx);
        }
    };


    struct IgnoreSet {
        std::vector<IgnoreRule> rules;

        bool is_ignored(std::string rel, bool is_dir) const {
            rel = normalize_rel_path(std::move(rel));
            if (rel.empty()) return false;

            // Hard-ignore internal UTX stuff (non negotiable)
            if (rel == deploy::kDeployFile) return true;
            if (rel.starts_with(std::string(deploy::kUtxDir) + "/")) return true;
            if (rel == deploy::kUtxDir) return true;

            bool ignored = false;
            for (const auto &rule: rules) {
                if (rule.matches(rel, is_dir)) {
                    ignored = !rule.negated;
                }
            }
            return ignored;
        }
    };


    class IgnoreManager {
    public:
        static std::expected<void, std::string> append_ignore_pattern(const fs::path &root, const std::string &pattern) {
            if (pattern.empty()) return std::unexpected("Empty pattern.");
            const fs::path p = root / kIgnoreFile;
            std::ofstream out(p, std::ios::app);
            if (!out) return std::unexpected("Failed to open .utxignore for append.");
            out << pattern << "\n";
            return {};
        }

        static IgnoreSet load_ignore_set(const fs::path &root) {
            IgnoreSet set;

            // Optional defaults (safe-ish)
            // NOTE: keep conservative, user can add more.
            auto add_default = [&](std::string pattern) {
                bool dir_only = pattern.ends_with('/');
                if (dir_only) pattern.pop_back();

                set.rules.push_back(IgnoreRule{
                    .negated = false,
                    .dir_only = dir_only,
                    .rx = std::regex(glob_to_regex(normalize_rel_path(pattern)))
                });
            };

            add_default(".git/"); // common
            add_default(".utxignore"); // common
            add_default("node_modules/"); // common
            add_default("build/"); // common
            add_default("cmake-build-*/"); // common

            const fs::path p = root / kIgnoreFile;
            if (!fs::exists(p)) return set;

            std::ifstream f(p);
            std::string line;
            while (std::getline(f, line)) {
                // trim spaces (simple)
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.
                        pop_back();
                size_t start = 0;
                while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) ++start;
                line = line.substr(start);

                if (line.empty()) continue;
                if (line.starts_with("#")) continue;

                bool neg = false;
                if (line.starts_with("!")) {
                    neg = true;
                    line.erase(0, 1);
                }

                bool dir_only = line.ends_with('/');
                if (dir_only) line.pop_back();

                line = normalize_rel_path(line);
                if (line.empty()) continue;

                set.rules.push_back(IgnoreRule{
                    .negated = neg,
                    .dir_only = dir_only,
                    .rx = std::regex(glob_to_regex(line))
                });
            }

            return set;
        }

        static std::string glob_to_regex(const std::string &pat) {
            // Supports: *, ?, ** (cross dirs)
            // Anchored full match.
            std::string r;
            r.reserve(pat.size() * 2);
            r += '^';

            for (size_t i = 0; i < pat.size(); ++i) {
                const char c = pat[i];

                // ** -> .*
                if (c == '*' && i + 1 < pat.size() && pat[i + 1] == '*') {
                    r += ".*";
                    ++i;
                    continue;
                }
                // * -> any except '/'
                if (c == '*') {
                    r += "[^/]*";
                    continue;
                }
                // ? -> single except '/'
                if (c == '?') {
                    r += "[^/]";
                    continue;
                }

                // escape regex specials
                if (std::string_view(R"(\.^$|()[]{}+)")
                    .find(c) != std::string_view::npos) {
                    r += '\\';
                }
                r += c;
            }

            r += '$';
            return r;
        }
    };
}
